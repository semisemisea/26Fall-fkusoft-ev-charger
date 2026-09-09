/**
 * @file charging_view.cpp
 * @brief 展示充电订单的真实计量与费用，管理订单轮询和停止充电交互。
 */
#include <evcharger/logging.h>

#include "charging_view.h"

#include "app/charge_poll_thread.h"
#include "common/format.h"
#include "models/charger.h"
#include "widgets/app_icons.h"
#include "widgets/charging_spinner_widget.h"
#include "widgets/toast.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(userChargingViewLog, "evcharger.user.charging", QtInfoMsg)

namespace {
	// 创建白色圆角信息卡片（样式见 style.qss #chargingMetricCard）
	/**
	 * @brief 创建充电指标使用的固定高度圆角卡片。
	 * @param parent 卡片的 Qt 父控件。
	 * @param height 卡片高度，单位像素。
	 * @return 已设置 chargingMetricCard 对象名的卡片，由 parent 拥有。
	 */
	QFrame *makeCard(QWidget *parent, int height) {
		auto *card = new QFrame(parent);
		card->setObjectName(QStringLiteral("chargingMetricCard"));
		card->setFixedHeight(height);
		return card;
	}
} // namespace

/**
 * @details 构造：搭建充电动效、两行信息卡片、预估费用与结束充电按钮
 */
ChargingView::ChargingView(ApiClient &api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("ChargingView"));
	EV_LOG_DEBUG(userChargingViewLog, this) << "View initialized";
	setAutoFillBackground(true);
	QPalette pal;
	QPixmap bg(QStringLiteral(":/backgrounds/bg.png"));
	pal.setBrush(QPalette::Window, QBrush(bg.scaled(390, 780, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
	setPalette(pal);

	m_spinnerWidget = new ChargingSpinnerWidget(this);
	m_spinnerWidget->setFixedSize(260, 260);

	// 第一行卡片：单价 / 已充入电量
	auto *row1 = new QHBoxLayout;
	row1->setContentsMargins(16, 0, 16, 0);
	row1->setSpacing(14);

	auto *rangeCard = makeCard(this, 84);
	auto *rangeLayout = new QVBoxLayout(rangeCard);
	rangeLayout->setContentsMargins(14, 8, 14, 10);
	rangeLayout->setSpacing(0);
	m_rangeValueLabel = new QLabel(rangeCard);
	m_rangeValueLabel->setTextFormat(Qt::RichText);
	rangeLayout->addWidget(m_rangeValueLabel);
	row1->addWidget(rangeCard, 1);

	auto *energyCard = makeCard(this, 84);
	auto *energyLayout = new QVBoxLayout(energyCard);
	energyLayout->setContentsMargins(14, 8, 14, 10);
	energyLayout->setSpacing(0);
	m_energyValueLabel = new QLabel(energyCard);
	m_energyValueLabel->setTextFormat(Qt::RichText);
	energyLayout->addWidget(m_energyValueLabel);
	row1->addWidget(energyCard, 1);

	// 第二行卡片：充电方式 / 充电桩编号
	auto *row2 = new QHBoxLayout;
	row2->setContentsMargins(16, 0, 16, 0);
	row2->setSpacing(14);

	auto *typeCard = makeCard(this, 68);
	auto *typeLayout = new QVBoxLayout(typeCard);
	typeLayout->setContentsMargins(14, 8, 14, 8);
	typeLayout->setSpacing(2);
	m_typeValueLabel = new QLabel(typeCard);
	m_typeValueLabel->setTextFormat(Qt::RichText);
	typeLayout->addWidget(m_typeValueLabel);
	row2->addWidget(typeCard, 1);

	auto *remainCard = makeCard(this, 68);
	auto *remainLayout = new QVBoxLayout(remainCard);
	remainLayout->setContentsMargins(14, 8, 14, 8);
	remainLayout->setSpacing(2);
	m_remainValueLabel = new QLabel(remainCard);
	m_remainValueLabel->setTextFormat(Qt::RichText);
	remainLayout->addWidget(m_remainValueLabel);
	row2->addWidget(remainCard, 1);

	// ===== 预估费用（样式见 style.qss #chargingFeeTitle / #chargingFeeValue）=====
	auto *feeTitleLabel = new QLabel(QStringLiteral("预估费用"), this);
	feeTitleLabel->setAlignment(Qt::AlignCenter);
	feeTitleLabel->setObjectName(QStringLiteral("chargingFeeTitle"));

	m_feeValueLabel = new QLabel(this);
	m_feeValueLabel->setAlignment(Qt::AlignCenter);
	m_feeValueLabel->setObjectName(QStringLiteral("chargingFeeValue"));

	// ===== 结束充电按钮（绿底黑字，圆角为高度一半；样式见 style.qss #chargingStopButton）=====
	m_stopButton = new QPushButton(QStringLiteral("结束充电"), this);
	m_stopButton->setObjectName(QStringLiteral("chargingStopButton"));
	m_stopButton->setFixedHeight(50);
	m_stopButton->setCursor(Qt::PointingHandCursor);

	// ===== 主布局 =====
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 20, 16, 16);
	layout->setSpacing(6);
	layout->addStretch(6);
	layout->addSpacing(50);
	layout->addWidget(m_spinnerWidget, 0, Qt::AlignCenter);
	layout->addSpacing(20);
	layout->addLayout(row1);
	layout->addLayout(row2);
	layout->addSpacing(6);
	layout->addWidget(feeTitleLabel);
	layout->addWidget(m_feeValueLabel);
	layout->addStretch(1);
	layout->addWidget(m_stopButton);

	m_pollThread = new ChargePollThread(this);
	connect(m_pollThread, &ChargePollThread::meterUpdated, this, [this](const QJsonObject &orderObject) {
		m_order = Order::fromJson(orderObject);
		updateDisplay(m_order);
	});
	connect(m_stopButton, &QPushButton::clicked, this, &ChargingView::stopCharging);
}

/**
 * @details 打开订单：立即刷新一次显示，并配置、启动可配置间隔的轮询线程
 */
void ChargingView::open(const Order &order) {
	EV_LOG_INFO(userChargingViewLog, this) << "Opening charging monitor; order_id=" << order.id;
	if (m_order.id == order.id && m_pollThread->isRunning()) {
		m_order = order;
		updateDisplay(order);
		return;
	}
	m_order = order;
	m_chargerType.clear();
	updateDisplay(order);
	m_api.get(QStringLiteral("/chargers/%1").arg(order.chargerId), [this, orderId = order.id](const QJsonValue &data, const QJsonObject &) {
                  // 旧订单的电桩详情可能晚于页面切换返回，不能覆盖当前订单的充电方式。
                  if (m_order.id != orderId) {
                      return;
                  }
                  m_chargerType = Charger::fromJson(data.toObject()).type;
                  updateDisplay(m_order); }, [](const ApiError &) { EV_LOG_WARNING(userChargingViewLog, nullptr) << "Background view refresh failed"; });
	if (m_pollThread->isRunning())
		m_pollThread->requestStop();
	m_pollThread->configure(order.id, m_api.baseUrl(), m_api.accessToken());
	m_pollThread->start();
}

/**
 * @details 页面隐藏时请求停止轮询线程
 */
void ChargingView::hideEvent(QHideEvent *event) {
	EV_LOG_INFO(userChargingViewLog, this) << "Stopping hidden charging monitor";
	QWidget::hideEvent(event);
	m_pollThread->requestStop();
}

/**
 * @details 用服务端订单数据刷新卡片与预估费用
 */
void ChargingView::updateDisplay(const Order &order) {
	// 单价（充电站统一价格）
	m_rangeValueLabel->setText(QStringLiteral(
								   "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>单价</div>"
								   "<div style='font-size:26px; color:#000000; line-height:1.0;'>￥%1"
								   "<span style='font-size:13px; color:#6b7280;'> /度</span></div>")
								   .arg(fenToYuan(order.unitPriceFenPerKwh)));

	// 已充入电量
	m_energyValueLabel->setText(QStringLiteral(
									"<div style='font-size:13px; color:#6b7280; line-height:1.0;'>已充入电量</div>"
									"<div style='font-size:26px; color:#000000; line-height:1.0;'>%1"
									"<span style='font-size:13px; color:#6b7280;'> kWh</span></div>")
									.arg(order.energyKwh, 0, 'f', 1));

	// 充电方式来自电桩详情；订单接口不返回电桩类型
	const QString typeText = m_chargerType == QLatin1String("fast")
								 ? QStringLiteral("快充")
							 : m_chargerType == QLatin1String("slow") ? QStringLiteral("慢充")
																	  : QStringLiteral("--");
	m_typeValueLabel->setText(QStringLiteral(
								  "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>充电方式</div>"
								  "<div style='font-size:18px; color:#000000; line-height:1.0;'>%1</div>")
								  .arg(typeText));

	// 充电桩编号
	m_remainValueLabel->setText(QStringLiteral(
									"<div style='font-size:13px; color:#6b7280; line-height:1.0;'>充电桩编号</div>"
									"<div style='font-size:20px; color:#000000; line-height:1.0;'>%1</div>")
									.arg(order.chargerCode));

	// 预估费用
	m_feeValueLabel->setText(QStringLiteral("￥%1").arg(fenToYuan(order.amountFen)));
}

/**
 * @details 弹确认框后请求停止充电；失败时恢复轮询并提示错误
 */
void ChargingView::stopCharging() {
	EV_LOG_INFO(userChargingViewLog, this) << "Stop charging requested";
	const auto choice = QMessageBox::question(this, QStringLiteral("停止充电"),
											  QStringLiteral("确定停止充电并生成账单吗？"));
	if (choice != QMessageBox::Yes) {
		return;
	}

	m_pollThread->requestStop();
	m_stopButton->setEnabled(false);
	m_api.post(QStringLiteral("/orders/%1/stop").arg(m_order.id), {}, [this](const QJsonValue &data, const QJsonObject &) {
                   m_stopButton->setEnabled(true);
                   EV_LOG_INFO(userChargingViewLog, this) << "Charging stopped successfully";
 emit orderStopped(Order::fromJson(data.toObject())); }, [this](const ApiError &error) {
 EV_LOG_WARNING(userChargingViewLog, this) << "API operation failed in view";
                   m_stopButton->setEnabled(true);
                   m_pollThread->configure(m_order.id, m_api.baseUrl(), m_api.accessToken());
                   m_pollThread->start();
                   Toast::error(this, error.message.isEmpty() ? error.code : error.message); });
}
