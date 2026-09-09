/**
 * @file station_detail_view.cpp
 * @brief 展示站内电桩并将预约、充电和导航意图交给主窗口。
 */
#include <evcharger/logging.h>

#include "station_detail_view.h"

#include "common/format.h"
#include "models/charger.h"
#include "widgets/app_icons.h"
#include "widgets/back_button.h"
#include "widgets/scale_button.h"
#include "widgets/spinner.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(userStationDetailViewLog, "evcharger.user.ui", QtInfoMsg)

namespace {
	// 切换电桩行的选中高亮：改动态属性后强制 QSS 重新匹配（#chargerRow[selected="true"]）
	/**
	 * @brief 更新电桩行 selected 属性并重新应用样式。
	 * @param row 需要更新高亮的电桩行。
	 * @param selected 是否选中该行。
	 */
	void setRowSelected(QFrame *row, bool selected) {
		row->setProperty("selected", selected);
		row->style()->unpolish(row);
		row->style()->polish(row);
	}
} // namespace

/**
 * @details 构造函数：搭建详情页全部控件与布局；按钮只发信号，实际下单/预约由 MainWindow 处理
 */
StationDetailView::StationDetailView(ApiClient &api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("StationDetailView"));
	EV_LOG_DEBUG(userStationDetailViewLog, this) << "View initialized";
	m_bgPixmap.load(QStringLiteral(":/backgrounds/station_detail_view.png"));
	m_backButton = new BackButton(this);

	m_nameLabel = new QLabel(this);
	m_nameLabel->setObjectName(QStringLiteral("heroTitle"));

	m_infoLabel = new QLabel(this);
	m_infoLabel->setObjectName(QStringLiteral("stationInfoText"));
	m_infoLabel->setWordWrap(true);

	m_distanceLabel = new QLabel(this);
	m_distanceLabel->setObjectName(QStringLiteral("stationTag"));
	m_distanceLabel->setTextFormat(Qt::RichText);
	m_distanceLabel->setAlignment(Qt::AlignCenter);
	m_distanceLabel->setFixedHeight(32);
	m_distanceLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

	m_priceLabel = new QLabel(this);
	m_priceLabel->setObjectName(QStringLiteral("stationTag"));
	m_priceLabel->setTextFormat(Qt::RichText);
	m_priceLabel->setAlignment(Qt::AlignCenter);
	m_priceLabel->setFixedHeight(32);
	m_priceLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

	m_availabilityLabel = new QLabel(this);
	m_availabilityLabel->setObjectName(QStringLiteral("stationTag"));
	m_availabilityLabel->setTextFormat(Qt::RichText);
	m_availabilityLabel->setAlignment(Qt::AlignCenter);
	m_availabilityLabel->setFixedHeight(32);
	m_availabilityLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

	m_navButton = new QPushButton(this);
	m_navButton->setFixedSize(64, 48);
	m_navButton->setIcon(AppIcons::turnRight(Qt::white, 24));
	m_navButton->setIconSize(QSize(24, 24));
	m_navButton->setCursor(Qt::PointingHandCursor);
	m_navButton->setObjectName(QStringLiteral("stationNavRoundButton"));

	m_statusLabel = new QLabel(this);
	m_statusLabel->setObjectName(QStringLiteral("error"));

	m_spinner = new Spinner(this);
	m_spinner->hide();

	auto *statusRow = new QWidget(this);
	auto *statusRowLayout = new QHBoxLayout(statusRow);
	statusRowLayout->setContentsMargins(0, 0, 0, 0);
	statusRowLayout->setSpacing(8);
	statusRowLayout->addStretch();
	statusRowLayout->addWidget(m_spinner);
	statusRowLayout->addWidget(m_statusLabel);
	statusRowLayout->addStretch();

	auto *chargersContainer = new QWidget(this);
	m_chargersLayout = new QVBoxLayout(chargersContainer);
	m_chargersLayout->setContentsMargins(0, 0, 0, 0);
	m_chargersLayout->setSpacing(10);
	m_chargersLayout->addStretch();

	auto *scrollArea = new QScrollArea(this);
	scrollArea->setWidgetResizable(true);
	scrollArea->setWidget(chargersContainer);

	m_backButton->move(12, 12);
	m_backButton->raise();

	auto *headerRow = new QHBoxLayout;
	headerRow->addWidget(m_nameLabel);
	headerRow->addStretch();

	m_bgSpacer = new QWidget(this);
	m_bgSpacer->setAttribute(Qt::WA_TransparentForMouseEvents);

	m_reserveButton = new QPushButton(QStringLiteral("预约"), this);
	m_reserveButton->setObjectName(QStringLiteral("reserveButton"));
	m_reserveButton->setCursor(Qt::PointingHandCursor);

	m_chargeButton = new QPushButton(QStringLiteral("充电"), this);
	m_chargeButton->setObjectName(QStringLiteral("chargeButton"));
	m_chargeButton->setCursor(Qt::PointingHandCursor);

	auto *bottomRow = new QHBoxLayout;
	bottomRow->setSpacing(12);
	bottomRow->addWidget(m_reserveButton);
	bottomRow->addWidget(m_chargeButton);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(20, 0, 20, 12);
	layout->addWidget(m_bgSpacer);
	layout->addLayout(headerRow);
	layout->addWidget(m_infoLabel);

	auto *tagRow = new QHBoxLayout;
	tagRow->setSpacing(8);
	tagRow->addWidget(m_distanceLabel, 0, Qt::AlignBottom);
	tagRow->addWidget(m_priceLabel, 0, Qt::AlignBottom);
	tagRow->addWidget(m_availabilityLabel, 0, Qt::AlignBottom);
	tagRow->addStretch();
	tagRow->addWidget(m_navButton, 0, Qt::AlignBottom);
	layout->addLayout(tagRow);

	layout->addWidget(statusRow);
	layout->addWidget(scrollArea, 1);
	layout->addLayout(bottomRow);

	connect(m_backButton, &QPushButton::clicked, this, &StationDetailView::backRequested);
	connect(m_navButton, &QPushButton::clicked, this,
			[this] { emit navigateRequested(m_station); });
	connect(m_reserveButton, &QPushButton::clicked, this, [this] {
		if (m_hasSelection && m_selectedCharger.status == QLatin1String("available"))
			emit reservationRequested(m_selectedCharger);
	});
	connect(m_chargeButton, &QPushButton::clicked, this, [this] {
		if (m_hasSelection && m_selectedCharger.status == QLatin1String("available"))
			emit chargeRequested(m_selectedCharger);
	});

	updateBottomButtons();

	if (!m_bgPixmap.isNull() && m_bgPixmap.width() > 0)
		m_bgSpacer->setFixedHeight(m_bgPixmap.height() * width() / m_bgPixmap.width());
}

/**
 * @details 打开电站：填充名称/地址/距离/价格/空闲标签，随后加载电桩列表
 */
void StationDetailView::open(const Station &station) {
	EV_LOG_INFO(userStationDetailViewLog, this) << "Opening station details";
	m_station = station;
	m_nameLabel->setText(station.name);
	m_infoLabel->setText(station.address);
	m_distanceLabel->setText(QStringLiteral(
								 "<span style='font-size:16px;font-weight:600;'>%1</span>"
								 "<span style='font-size:12px;'> km</span>")
								 .arg(QString::number(station.distanceKm, 'f', 1)));

	m_priceLabel->setText(QStringLiteral(
							  "<span style='font-size:12px;'>￥ </span>"
							  "<span style='font-size:16px;font-weight:600;'>%1</span>"
							  "<span style='font-size:12px;'> /度</span>")
							  .arg(fenToYuan(station.pricePerKwhFen)));
	m_availabilityLabel->setText(QStringLiteral(
									 "<span style='font-size:12px;'>空闲 </span>"
									 "<span style='font-size:16px;font-weight:600;'>%1/%2</span>")
									 .arg(station.availableChargerCount)
									 .arg(station.chargerCount));
	loadChargers();
}

/**
 * @details 拉取站内电桩并逐行渲染（编号、状态徽标、类型、功率）；行可点击选中
 */
void StationDetailView::loadChargers() {
	EV_LOG_INFO(userStationDetailViewLog, this) << "Loading station chargers";
	m_spinner->show();
	m_statusLabel->hide();

	m_hasSelection = false;
	m_selectedRow = nullptr;
	m_chargers.clear();
	updateBottomButtons();

	m_api.get(QStringLiteral("/stations/%1/chargers?pageSize=100").arg(m_station.id), [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  while (m_chargersLayout->count() > 1) {
                      QLayoutItem *item = m_chargersLayout->takeAt(0);
                      item->widget()->deleteLater();
                      delete item;
                  }
                  const QJsonArray chargers = data.toArray();
                  for (const QJsonValue &value : chargers) {
                      const Charger charger = Charger::fromJson(value.toObject());
                      m_chargers.append(charger);

                      auto *row = new QFrame(this);
                      row->setObjectName(QStringLiteral("chargerRow"));
                      row->setProperty("chargerId", charger.id);
                      row->installEventFilter(this);
                      row->setCursor(Qt::PointingHandCursor);

                      auto *rowLayout = new QVBoxLayout(row);
                      rowLayout->setContentsMargins(12, 10, 12, 10);
                      rowLayout->setSpacing(6);

                      auto *topRow = new QHBoxLayout;
                      topRow->setSpacing(8);

                      auto *codeLabel = new QLabel(charger.code, row);
                      codeLabel->setObjectName(QStringLiteral("chargerCodeText"));
                      topRow->addWidget(codeLabel);

                      // 状态徽章样式按 chargerTag_<status> 命名约定匹配 style.qss
                      auto *statusLabel = new QLabel(Charger::statusLabel(charger.status), row);
                      static const QStringList kKnownStatuses{QLatin1String("available"), QLatin1String("charging"),
                                                              QLatin1String("reserved"), QLatin1String("fault"),
                                                              QLatin1String("offline")};
                      statusLabel->setObjectName(QStringLiteral("chargerTag_")
                                                  + (kKnownStatuses.contains(charger.status) ? charger.status
                                                                                             : QStringLiteral("default")));
                      topRow->addWidget(statusLabel);
                      topRow->addStretch();

                      rowLayout->addLayout(topRow);

                      auto *detailRow = new QHBoxLayout;
                      detailRow->setSpacing(12);

                      auto *typeLabel = new QLabel(Charger::typeLabel(charger), row);
                      typeLabel->setObjectName(QStringLiteral("chargerMetaText"));
                      detailRow->addWidget(typeLabel);

                      auto *powerLabel = new QLabel(
                          QStringLiteral("%1 kW").arg(charger.powerKw, 0, 'f', 1), row);
                      powerLabel->setObjectName(QStringLiteral("chargerMetaText"));
                      detailRow->addWidget(powerLabel);
                      detailRow->addStretch();

                      rowLayout->addLayout(detailRow);

                      m_chargersLayout->insertWidget(m_chargersLayout->count() - 1, row);
                  }
                  if (chargers.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("站内暂无电桩"));
                      m_statusLabel->show();
                  } }, [this](const ApiError &error) {
 EV_LOG_WARNING(userStationDetailViewLog, this) << "API operation failed in view";
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}

/**
 * @details 处理电桩行点击：取消旧选中样式，记录选中电桩并高亮该行
 */
bool StationDetailView::eventFilter(QObject *obj, QEvent *event) {
	if (event->type() == QEvent::MouseButtonPress) {
		QVariant prop = obj->property("chargerId");
		if (prop.isValid()) {
			int chargerId = prop.toInt();
			for (const Charger &c : m_chargers) {
				if (c.id == chargerId) {
					if (m_selectedRow)
						setRowSelected(m_selectedRow, false);
					m_selectedCharger = c;
					m_hasSelection = true;
					m_selectedRow = qobject_cast<QFrame *>(obj);
					if (m_selectedRow) {
						setRowSelected(m_selectedRow, true);
					}
					updateBottomButtons();
					break;
				}
			}
			return true;
		}
	}
	return QWidget::eventFilter(obj, event);
}

/**
 * @details 仅当选中的电桩为 available 时启用预约/充电按钮；禁用态灰色外观由 QSS :disabled 规则接管
 */
void StationDetailView::updateBottomButtons() {
	bool available = m_hasSelection &&
					 m_selectedCharger.status == QLatin1String("available");

	m_reserveButton->setEnabled(available);
	m_chargeButton->setEnabled(available);
}

/**
 * @details 按控件宽度等比绘制顶部背景图，图片下方用渐变补齐剩余高度
 */
void StationDetailView::paintEvent(QPaintEvent *event) {
	QWidget::paintEvent(event);
	if (m_bgPixmap.isNull())
		return;
	QPainter painter(this);
	const int targetWidth = width();
	const int targetHeight = m_bgPixmap.height() * targetWidth / m_bgPixmap.width();
	painter.drawPixmap(QRect(0, 0, targetWidth, targetHeight), m_bgPixmap);

	if (targetHeight < height()) {
		QLinearGradient gradient(0, targetHeight, 0, height());
		gradient.setColorAt(0, QColor(QStringLiteral("#f6f7f8")));
		gradient.setColorAt(1, QColor(QStringLiteral("#e9ebed")));
		painter.fillRect(0, targetHeight, width(), height() - targetHeight, gradient);
	}
}

/**
 * @details 尺寸变化时同步调整背景占位高度，保证背景图不被内容遮挡变形
 */
void StationDetailView::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	if (m_bgSpacer && !m_bgPixmap.isNull() && m_bgPixmap.width() > 0) {
		const int h = m_bgPixmap.height() * width() / m_bgPixmap.width();
		m_bgSpacer->setFixedHeight(h);
	}
}
