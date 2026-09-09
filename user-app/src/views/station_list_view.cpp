/**
 * @file station_list_view.cpp
 * @brief 查询附近电站，支持地图选址、手动坐标、文本过滤和基于空闲率的本地推荐。
 */
#include "../../../common/refresh/page_refresh.h"
#include <evcharger/logging.h>

#include "station_list_view.h"

#include "common/demo.h"
#include "common/theme.h"
#include "widgets/app_icons.h"
#include "widgets/spinner.h"
#include "widgets/station_card.h"

#include "evcharger/map_picker_dialog.h"
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cmath>

Q_LOGGING_CATEGORY(userStationListViewLog, "evcharger.user.ui", QtInfoMsg)

/**
 * @details 构造函数：搭建定位切换、搜索框、AI 推荐横幅与电站卡片滚动列表
 */
StationListView::StationListView(Session &session, ApiClient &api, QWidget *parent)
	: QWidget(parent), m_session(session), m_api(api) {
	new evcharger::PageRefresh(this, [this] { if (m_locationConfirmed && !m_refreshPending) reload(); }, true);
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("StationListView"));
	EV_LOG_DEBUG(userStationListViewLog, this) << "View initialized";
	// ===== 定位行：图标 + 文字 =====
	auto *locationWidget = new QWidget(this);
	auto *locationLayout = new QHBoxLayout(locationWidget);
	locationLayout->setContentsMargins(0, 0, 0, 0);
	locationLayout->setSpacing(0);

	auto *locationIcon = new QLabel(this);
	QPixmap pinPixmap = AppIcons::pin(theme::textSecondary(), 18, false);
	locationIcon->setPixmap(pinPixmap);

	m_locationLabel = new QLabel(this);
	m_locationLabel->setObjectName(QStringLiteral("searchLocation"));
	m_locationLabel->setWordWrap(true);
	m_locationLabel->setTextFormat(Qt::PlainText);
	m_locationLabel->setText(QStringLiteral("请地图选址或填写经纬度，然后确认位置"));

	locationLayout->addWidget(locationIcon);
	locationLayout->addWidget(m_locationLabel);
	locationLayout->addStretch();

	auto *mapButton = new QPushButton(QStringLiteral("地图选址"), this);
	mapButton->setObjectName(QStringLiteral("pickLocationButton"));
	connect(mapButton, &QPushButton::clicked, this, &StationListView::pickLocation);

	m_latitudeEdit = new QLineEdit(QString::number(m_session.latitude(), 'f', 6), this);
	m_latitudeEdit->setObjectName(QStringLiteral("latitudeEdit"));
	m_latitudeEdit->setPlaceholderText(QStringLiteral("纬度 -90～90"));
	m_longitudeEdit = new QLineEdit(QString::number(m_session.longitude(), 'f', 6), this);
	m_longitudeEdit->setObjectName(QStringLiteral("longitudeEdit"));
	m_longitudeEdit->setPlaceholderText(QStringLiteral("经度 -180～180"));
	auto *coordinateButton = new QPushButton(QStringLiteral("确认位置"), this);
	coordinateButton->setObjectName(QStringLiteral("searchCoordinatesButton"));
	auto *coordinateRow = new QHBoxLayout;
	coordinateRow->addWidget(new QLabel(QStringLiteral("纬度"), this));
	coordinateRow->addWidget(m_latitudeEdit, 1);
	coordinateRow->addWidget(new QLabel(QStringLiteral("经度"), this));
	coordinateRow->addWidget(m_longitudeEdit, 1);

	connect(coordinateButton, &QPushButton::clicked, this, &StationListView::searchCoordinates);
	connect(m_latitudeEdit, &QLineEdit::returnPressed, this, &StationListView::searchCoordinates);
	connect(m_longitudeEdit, &QLineEdit::returnPressed, this, &StationListView::searchCoordinates);

	auto *sectionTitle = new QLabel(QStringLiteral("附近充电站（50 公里内，按直线距离由近到远）"), this);
	sectionTitle->setObjectName(QStringLiteral("meta"));
	sectionTitle->setWordWrap(true);

	// ===== 搜索框：图标在输入框内部左侧 =====
	m_searchEdit = new QLineEdit(this);
	m_searchEdit->setPlaceholderText(QStringLiteral("在结果中筛选站名 / 地址"));
	m_searchEdit->setClearButtonEnabled(true);
	connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { applyFilter(); });

	// 添加搜索图标到输入框内部左侧
	QPixmap searchPixmap = AppIcons::search(theme::textSecondary(), 18, false);
	QIcon searchIcon(searchPixmap);
	m_searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);

	m_bannerButton = new QPushButton(this);
	m_bannerButton->setObjectName(QStringLiteral("aiBanner"));
	m_bannerButton->setCursor(Qt::PointingHandCursor);
	m_bannerButton->hide();

	m_statusLabel = new QLabel(this);
	m_statusLabel->setObjectName(QStringLiteral("error"));
	m_statusLabel->setWordWrap(true);
	m_statusLabel->setTextFormat(Qt::PlainText);
	m_statusLabel->hide();

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

	auto *cardsContainer = new QWidget(this);
	m_cardsLayout = new QVBoxLayout(cardsContainer);
	m_cardsLayout->setContentsMargins(0, 0, 0, 0);
	m_cardsLayout->setSpacing(10);
	m_cardsLayout->addStretch();

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setWidget(cardsContainer);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(10);
	layout->addWidget(locationWidget);
	layout->addWidget(mapButton);
	layout->addLayout(coordinateRow);
	layout->addWidget(coordinateButton);
	layout->addWidget(sectionTitle);
	layout->addWidget(m_searchEdit);
	layout->addWidget(m_bannerButton);
	layout->addWidget(statusRow);
	layout->addWidget(m_scrollArea);

	connect(m_bannerButton, &QPushButton::clicked, this, [this] {
		if (m_hasRecommendation) {
			emit stationSelected(m_recommendedStation);
		}
	});
}

/**
 * @details 页面显示时自动刷新列表
 */
void StationListView::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
}

/**
 * @details 按当前定位请求附近电站并重建卡片；首屏成功后播放一次淡入动画
 */
void StationListView::reload() {
	EV_LOG_INFO(userStationListViewLog, this) << "Loading stations";
	m_refreshPending = true;
	const quint64 generation = beginSearch();
	const QPointer<StationListView> guard(this);

	QUrlQuery query;
	query.addQueryItem(QLatin1String("latitude"), QString::number(m_session.latitude(), 'f', 6));
	query.addQueryItem(QLatin1String("longitude"), QString::number(m_session.longitude(), 'f', 6));
	query.addQueryItem(QLatin1String("radiusKm"), QStringLiteral("50"));

	query.addQueryItem(QStringLiteral("sort"), QStringLiteral("distance"));

	const auto onSuccess = [this, guard, generation](const QJsonValue &data, const QJsonObject &) {
		if (!guard || generation != m_requestGeneration)
			return;
		m_refreshPending = false;
		m_spinner->hide();
		const QJsonArray stations = data.toArray();
		for (const QJsonValue &value : stations) {
			auto *card = new StationCard(Station::fromJson(value.toObject()), this);
			connect(card, &StationCard::clicked, this, &StationListView::stationSelected);
			connect(card, &StationCard::navigateRequested, this, &StationListView::navigateRequested);
			m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
			m_cards.append(card);
		}
		if (stations.isEmpty()) {
			m_statusLabel->setText(QStringLiteral("该位置 50 公里内暂无充电站"));
			m_statusLabel->show();
			return;
		}
		applyFilter();
		loadRecommendation();
		if (!m_listAnimated) {
			m_listAnimated = true;
			auto *fade = new QGraphicsOpacityEffect(m_scrollArea);
			m_scrollArea->setGraphicsEffect(fade);
			fade->setOpacity(0.0);
			auto *anim = new QPropertyAnimation(fade, "opacity", m_scrollArea);
			anim->setDuration(demo::ms(220));
			anim->setStartValue(0.0);
			anim->setEndValue(1.0);
			anim->setEasingCurve(QEasingCurve::OutQuint);
			connect(anim, &QPropertyAnimation::finished, m_scrollArea, [this] {
				m_scrollArea->setGraphicsEffect(nullptr);
			});
			anim->start(QAbstractAnimation::DeleteWhenStopped);
		}
	};
	const auto onFailure = [this, guard, generation](const ApiError &error) {
		if (!guard || generation != m_requestGeneration)
			return;
		m_refreshPending = false;
		EV_LOG_WARNING(userStationListViewLog, this) << "API operation failed in view";
		m_spinner->hide();
		m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
		m_statusLabel->show();
	};
	m_api.get(QStringLiteral("/stations/nearby?%1").arg(query.toString(QUrl::FullyEncoded)), onSuccess, onFailure);
}

quint64 StationListView::beginSearch() {
	++m_requestGeneration;
	qDeleteAll(m_cards);
	m_cards.clear();
	m_spinner->show();
	m_statusLabel->hide();
	m_bannerButton->hide();
	m_hasRecommendation = false;
	return m_requestGeneration;
}

void StationListView::useLocation(double latitude, double longitude) {
	m_locationConfirmed = true;
	m_session.setLocation(latitude, longitude);
	m_latitudeEdit->setText(QString::number(latitude, 'f', 6));
	m_longitudeEdit->setText(QString::number(longitude, 'f', 6));
	m_locationLabel->setText(QStringLiteral("查询中心：纬度 %1，经度 %2")
								 .arg(latitude, 0, 'f', 6)
								 .arg(longitude, 0, 'f', 6));
	m_searchEdit->clear();
	reload();
}

void StationListView::searchCoordinates() {
	bool latitudeOk = false;
	bool longitudeOk = false;
	const double latitude = m_latitudeEdit->text().trimmed().toDouble(&latitudeOk);
	const double longitude = m_longitudeEdit->text().trimmed().toDouble(&longitudeOk);
	if (!latitudeOk || !longitudeOk || !std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
		m_statusLabel->setText(QStringLiteral("请输入有效坐标：纬度 -90～90，经度 -180～180"));
		m_statusLabel->show();
		return;
	}
	useLocation(latitude, longitude);
}

void StationListView::pickLocation() {
	bool latitudeOk = false;
	bool longitudeOk = false;
	const double latitude = m_latitudeEdit->text().trimmed().toDouble(&latitudeOk);
	const double longitude = m_longitudeEdit->text().trimmed().toDouble(&longitudeOk);
	const bool valid = latitudeOk && longitudeOk && std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= 90 && std::abs(longitude) <= 180;
	MapPickerDialog picker(qEnvironmentVariable("TENCENT_MAP_KEY"),
						   valid ? latitude : m_session.latitude(), valid ? longitude : m_session.longitude(), this, window()->size());
	if (picker.exec() != QDialog::Accepted)
		return;
	m_latitudeEdit->setText(QString::number(picker.latitude(), 'f', 6));
	m_longitudeEdit->setText(QString::number(picker.longitude(), 'f', 6));
	m_statusLabel->hide();
}

/**
 * @details 按搜索关键字逐卡片匹配（名称/地址），仅切换可见性
 */
void StationListView::applyFilter() {
	EV_LOG_INFO(userStationListViewLog, this) << "Applying station filter";
	const QString filter = m_searchEdit->text().trimmed();
	for (StationCard *card : m_cards) {
		card->setVisible(card->matches(filter));
	}
}

/**
 * @details 根据当前空闲率推荐可用电站，不依赖本期范围之外的预测接口
 */
void StationListView::loadRecommendation() {
	EV_LOG_INFO(userStationListViewLog, this) << "Loading station recommendation";
	const Station *best = nullptr;
	double bestRatio = -1;
	for (const StationCard *card : m_cards) {
		const Station &station = card->station();
		if (station.chargerCount == 0) {
			continue;
		}
		const double ratio = double(station.availableChargerCount) / station.chargerCount;
		if (ratio > bestRatio) {
			best = &station;
			bestRatio = ratio;
		}
	}
	if (best && bestRatio > 0) {
		m_recommendedStation = *best;
		m_hasRecommendation = true;
		m_bannerButton->setText(
			QStringLiteral("推荐：%1 · 当前空闲率较高").arg(best->name));
		m_bannerButton->setEnabled(true);
		m_bannerButton->show();
	}
}
