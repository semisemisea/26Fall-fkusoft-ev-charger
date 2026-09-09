/**
 * @file info_pages.cpp
 * @brief 实现历史订单、钱包流水、预约记录与关于系统页面。
 */
#include <evcharger/logging.h>

#include "info_pages.h"

#include "common/format.h"
#include "common/theme.h"
#include "models/order.h"
#include "models/reservation.h"
#include "widgets/app_icons.h"
#include "widgets/back_button.h"
#include "widgets/spinner.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(userInfoPagesLog, "evcharger.user.ui", QtInfoMsg)

namespace {
	// 搭建通用页面骨架：返回键 + 标题 + 加载指示 + 滚动列表，返回状态标签
	/**
	 * @brief 搭建历史页公用骨架，并输出列表布局与加载指示器。
	 * @param title 页头显示的标题。
	 * @param onBack 点击返回键时执行的回调，以 parent 为连接上下文。
	 * @param[out] listLayout 接收可插入业务卡片的布局，不可为空。
	 * @param[out] spinnerOut 接收加载指示器，不可为空。
	 * @param parent 页面控件，拥有创建出的布局及子控件。
	 * @return 初始隐藏的状态标签，由 parent 的对象树管理。
	 */
	QLabel *makePageShell(const QString &title, const std::function<void()> &onBack,
						  QVBoxLayout **listLayout, Spinner **spinnerOut, QWidget *parent) {
		auto *backButton = new BackButton(parent);
		auto *titleLabel = new QLabel(title, parent);
		titleLabel->setObjectName(QStringLiteral("pageTitle"));

		auto *statusLabel = new QLabel(parent);
		statusLabel->setObjectName(QStringLiteral("muted"));
		statusLabel->hide();

		auto *spinner = new Spinner(parent);
		spinner->hide();
		*spinnerOut = spinner;

		auto *statusRow = new QWidget(parent);
		auto *statusRowLayout = new QHBoxLayout(statusRow);
		statusRowLayout->setContentsMargins(0, 0, 0, 0);
		statusRowLayout->setSpacing(8);
		statusRowLayout->addStretch();
		statusRowLayout->addWidget(spinner);
		statusRowLayout->addWidget(statusLabel);
		statusRowLayout->addStretch();

		auto *container = new QWidget(parent);
		*listLayout = new QVBoxLayout(container);
		(*listLayout)->setContentsMargins(0, 0, 0, 0);
		(*listLayout)->setSpacing(10);
		(*listLayout)->addWidget(statusRow);
		(*listLayout)->addStretch();

		auto *scrollArea = new QScrollArea(parent);
		scrollArea->setWidgetResizable(true);
		scrollArea->setWidget(container);

		// 两侧预留等宽空间，让标题相对页面居中，不受返回按钮挤压。
		auto *headerRow = new QGridLayout;
		headerRow->setHorizontalSpacing(0);
		headerRow->setColumnMinimumWidth(0, backButton->width());
		headerRow->setColumnMinimumWidth(2, backButton->width());
		headerRow->setColumnStretch(1, 1);
		headerRow->addWidget(backButton, 0, 0);
		headerRow->addWidget(titleLabel, 0, 1, Qt::AlignCenter);

		auto *layout = new QVBoxLayout(parent);
		layout->setContentsMargins(12, 12, 12, 12);
		layout->setSpacing(10);
		layout->addLayout(headerRow);
		layout->addWidget(scrollArea);

		QObject::connect(backButton, &QPushButton::clicked, parent, onBack);
		return statusLabel;
	}

	// 清空列表中的卡片（保留状态行与底部 stretch）
	/**
	 * @brief 删除旧业务卡片，同时保留状态行和末尾弹性项。
	 * @param listLayout 由 makePageShell 创建的列表布局；业务卡片位于索引 1 起。
	 * @details 控件通过 deleteLater 释放，取出的布局项立即删除；两者具有不同生命周期。
	 */
	void clearCards(QVBoxLayout *listLayout) {
		while (listLayout->count() > 2) {
			QLayoutItem *item = listLayout->takeAt(1);
			item->widget()->deleteLater();
			delete item;
		}
	}
} // namespace

/**
 * @details 构造历史订单页骨架
 */
OrderHistoryView::OrderHistoryView(ApiClient &api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("OrderHistoryView"));
	EV_LOG_DEBUG(userInfoPagesLog, this) << "View initialized";
	QVBoxLayout *listLayout = nullptr;
	m_statusLabel = makePageShell(QStringLiteral("历史充电订单"), [this] { emit backRequested(); }, &listLayout, &m_spinner, this);
	m_listLayout = listLayout;
}

/**
 * @details 页面显示时加载订单列表
 */
void OrderHistoryView::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	load();
}

/**
 * @details 拉取历史订单（GET /orders）并按状态着色渲染卡片
 */
void OrderHistoryView::load() {
	EV_LOG_INFO(userInfoPagesLog, this) << "Loading account history";
	m_spinner->show();
	m_statusLabel->hide();
	m_api.get(QStringLiteral("/orders?pageSize=50"), [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray orders = data.toArray();
                  if (orders.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无历史订单"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : orders) {
                      const Order order = Order::fromJson(value.toObject());

                      // 状态文字颜色按语义类匹配 style.qss（#inkSuccess/#inkWarning/#inkPrimary）
                      const QLatin1String statusInkClass = order.status == QLatin1String("settled")
                      ? QLatin1String("inkSuccess")
                      : (order.status == QLatin1String("awaiting_payment") ? QLatin1String("inkWarning")
                                                                            : QLatin1String("inkPrimary"));

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *nameLabel = new QLabel(order.stationName, card);
                      nameLabel->setObjectName(QStringLiteral("cardTitle"));
                      auto *statusLabel = new QLabel(Order::statusLabel(order.status), card);
                      statusLabel->setObjectName(statusInkClass);
                      auto *detailLabel = new QLabel(
                          QStringLiteral("电桩 %1 · %2")
                              .arg(order.chargerCode,
                                   QString(order.startedAt).replace(QLatin1Char('T'), QLatin1Char(' ')).left(16)),
                          card);
                      detailLabel->setObjectName(QStringLiteral("meta"));
                      auto *durationLabel = new QLabel(QStringLiteral("%1 分钟").arg(order.durationMinutes), card);
                      durationLabel->setObjectName(QStringLiteral("meta"));
                      auto *amountLabel = new QLabel(
                          QStringLiteral("%1 度 · ￥%2")
                              .arg(order.energyKwh, 0, 'f', 1)
                              .arg(fenToYuan(order.amountFen)),
                          card);
                      amountLabel->setObjectName(QStringLiteral("strong"));
                      amountLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(nameLabel, 0, 0);
                      grid->addWidget(statusLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(detailLabel, 1, 0, 1, 2);
                      grid->addWidget(durationLabel, 2, 0);
                      grid->addWidget(amountLabel, 2, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  } }, [this](const ApiError &error) {
 EV_LOG_WARNING(userInfoPagesLog, this) << "API operation failed in view";
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}

/**
 * @details 构造钱包流水页骨架
 */
TransactionsView::TransactionsView(ApiClient &api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("TransactionsView"));
	EV_LOG_DEBUG(userInfoPagesLog, this) << "View initialized";
	QVBoxLayout *listLayout = nullptr;
	m_statusLabel = makePageShell(QStringLiteral("钱包流水"), [this] { emit backRequested(); }, &listLayout, &m_spinner, this);
	m_listLayout = listLayout;
}

/**
 * @details 页面显示时加载流水列表
 */
void TransactionsView::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	load();
}

/**
 * @details 拉取钱包流水并渲染卡片，金额正负用绿 / 红区分
 */
void TransactionsView::load() {
	EV_LOG_INFO(userInfoPagesLog, this) << "Loading account history";
	m_spinner->show();
	m_statusLabel->hide();
	m_api.get(QStringLiteral("/me/wallet/transactions"), [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray transactions = data.toArray();
                  if (transactions.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无流水记录"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : transactions) {
                      const QJsonObject object = value.toObject();
                      QString type;
                      const QString typeCode = object.value(QLatin1String("type")).toString();
                      if (typeCode == QLatin1String("top_up")) {
                          type = QStringLiteral("充值");
                      } else if (typeCode == QLatin1String("charge_debit")) {
                          type = QStringLiteral("充电扣款");
                      } else if (typeCode == QLatin1String("refund")) {
                          type = QStringLiteral("退款");
                      } else {
                          type = QStringLiteral("调整");
                      }
                      const qlonglong amount = object.value(QLatin1String("amountFen")).toInteger();

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *typeLabel = new QLabel(type, card);
                      typeLabel->setObjectName(QStringLiteral("strong"));
                      auto *timeLabel = new QLabel(
                          QString(object.value(QLatin1String("createdAt")).toString())
                              .replace(QLatin1Char('T'), QLatin1Char(' '))
                              .left(16),
                          card);
                      timeLabel->setObjectName(QStringLiteral("meta"));
                      auto *amountLabel = new QLabel(
                          QStringLiteral("%1￥%2")
                              .arg(amount >= 0 ? QStringLiteral("+") : QStringLiteral("-"),
                                   fenToYuan(qAbs(amount))),
                          card);
                      amountLabel->setObjectName(amount >= 0 ? QStringLiteral("txIncome")
                                                             : QStringLiteral("txExpense"));
                      amountLabel->setAlignment(Qt::AlignRight);
                      auto *balanceLabel = new QLabel(
                          QStringLiteral("余额 ￥%1")
                              .arg(fenToYuan(object.value(QLatin1String("balanceAfterFen")).toInteger())),
                          card);
                      balanceLabel->setObjectName(QStringLiteral("meta"));
                      balanceLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(typeLabel, 0, 0);
                      grid->addWidget(amountLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(timeLabel, 1, 0);
                      grid->addWidget(balanceLabel, 1, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  } }, [this](const ApiError &error) {
 EV_LOG_WARNING(userInfoPagesLog, this) << "API operation failed in view";
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}

/**
 * @details 构造预约记录页骨架
 */
ReservationHistoryView::ReservationHistoryView(ApiClient &api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("ReservationHistoryView"));
	EV_LOG_DEBUG(userInfoPagesLog, this) << "View initialized";
	QVBoxLayout *listLayout = nullptr;
	m_statusLabel = makePageShell(QStringLiteral("我的预约记录"), [this] { emit backRequested(); }, &listLayout, &m_spinner, this);
	m_listLayout = listLayout;
}

/**
 * @details 页面显示时加载预约记录
 */
void ReservationHistoryView::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	load();
}

/**
 * @details 拉取预约记录（GET /reservations）并按状态着色渲染卡片
 */
void ReservationHistoryView::load() {
	EV_LOG_INFO(userInfoPagesLog, this) << "Loading account history";
	m_spinner->show();
	m_statusLabel->hide();
	m_api.get(QStringLiteral("/reservations"), [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray reservations = data.toArray();
                  if (reservations.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无预约记录"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : reservations) {
                      const Reservation reservation = Reservation::fromJson(value.toObject());

                      // 状态文字颜色按语义类匹配 style.qss
                      const QLatin1String statusInkClass = reservation.status == QLatin1String("active")
                      ? QLatin1String("inkPrimary")
                      : (reservation.status == QLatin1String("used")
                             ? QLatin1String("inkSuccess")
                             : (reservation.status == QLatin1String("expired") ? QLatin1String("inkWarning")
                                                                                : QLatin1String("inkMuted")));

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *nameLabel = new QLabel(reservation.stationName, card);
                      nameLabel->setObjectName(QStringLiteral("cardTitle"));
                      auto *statusLabel = new QLabel(Reservation::statusLabel(reservation.status), card);
                      statusLabel->setObjectName(statusInkClass);
                      auto *detailLabel = new QLabel(
                          QStringLiteral("电桩 %1 · %2")
                              .arg(reservation.chargerCode,
                                   QString(reservation.startAt)
                                       .replace(QLatin1Char('T'), QLatin1Char(' '))
                                       .left(16)),
                          card);
                      detailLabel->setObjectName(QStringLiteral("meta"));
                      auto *expireLabel = new QLabel(
                          QStringLiteral("到期 %1")
                              .arg(reservation.expiresAt.toString(QStringLiteral("MM-dd hh:mm"))),
                          card);
                      expireLabel->setObjectName(QStringLiteral("meta"));
                      expireLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(nameLabel, 0, 0);
                      grid->addWidget(statusLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(detailLabel, 1, 0);
                      grid->addWidget(expireLabel, 1, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  } }, [this](const ApiError &error) {
 EV_LOG_WARNING(userInfoPagesLog, this) << "API operation failed in view";
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}

/**
 * @details 构造“关于系统”静态展示页
 */
AboutView::AboutView(QWidget *parent)
	: QWidget(parent) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("AboutView"));
	EV_LOG_DEBUG(userInfoPagesLog, this) << "View initialized";
	auto *backButton = new BackButton(this);
	connect(backButton, &QPushButton::clicked, this, &AboutView::backRequested);
	auto *headerRow = new QHBoxLayout;
	headerRow->addWidget(backButton);
	headerRow->addStretch();

	auto *iconLabel = new QLabel(this);
	iconLabel->setAlignment(Qt::AlignCenter);
	iconLabel->setObjectName(QStringLiteral("heroIconSmall"));
	iconLabel->setPixmap(QPixmap(QStringLiteral(":/backgrounds/logo.png"))
							 .scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	iconLabel->setFixedSize(160, 160);

	auto *nameLabel = new QLabel(QStringLiteral("智能充电系统"), this);
	nameLabel->setAlignment(Qt::AlignCenter);
	nameLabel->setObjectName(QStringLiteral("heroTitle"));

	auto *versionLabel = new QLabel(QStringLiteral("版本 1.0.0"), this);
	versionLabel->setAlignment(Qt::AlignCenter);
	versionLabel->setObjectName(QStringLiteral("muted"));

	auto *detailLabel = new QLabel(QStringLiteral("电动汽车充电桩应用管理平台 · 用户端\n基于 Qt 6 构建"), this);
	detailLabel->setAlignment(Qt::AlignCenter);
	detailLabel->setObjectName(QStringLiteral("muted"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->addLayout(headerRow);
	layout->addStretch(2);
	layout->addWidget(iconLabel, 0, Qt::AlignHCenter);
	layout->addWidget(nameLabel);
	layout->addWidget(versionLabel);
	layout->addSpacing(12);
	layout->addWidget(detailLabel);
	layout->addStretch(3);
}
