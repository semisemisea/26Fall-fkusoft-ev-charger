#include "navigation_view.h"
#include "widgets/back_button.h"

#include <QButtonGroup>
#include <QHideEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#ifdef USER_APP_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

#ifdef USER_APP_HAS_WEBENGINE
namespace {
	// 自有页面不允许跳转至外部地图、App 或小程序。
	class RoutePage final : public QWebEnginePage {
	public:
		using QWebEnginePage::QWebEnginePage;

	protected:
		bool acceptNavigationRequest(const QUrl &url, NavigationType, bool isMainFrame) override {
			return !isMainFrame || url.scheme() == QStringLiteral("qrc");
		}
		QWebEnginePage *createWindow(WebWindowType) override { return nullptr; }
	};
} // namespace
#endif

NavigationView::NavigationView(Session &session, ApiClient &api, QWidget *parent)
	: QWidget(parent), m_session(session), m_api(api) {
	setObjectName(QStringLiteral("NavigationView"));
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(12, 12, 12, 12);
	auto *header = new QHBoxLayout;
	auto *back = new BackButton(this);
	m_title = new QLabel(this);
	header->addWidget(back);
	header->addWidget(m_title, 1);
	m_layout->addLayout(header);

	// 替代原第三方地图内的方式切换，仍复用已有路线请求。
	auto *modes = new QButtonGroup(this);
	for (const auto &mode : {QStringLiteral("driving"), QStringLiteral("walking")}) {
		auto *button = new QPushButton(mode == QStringLiteral("driving") ? tr("驾车") : tr("步行"), this);
		button->setObjectName(mode);
		button->setCheckable(true);
		button->setChecked(mode == m_mode);
		modes->addButton(button);
		header->addWidget(button);
		connect(button, &QPushButton::clicked, this, [this, mode] {
			if (m_mode == mode)
				return;
			m_mode = mode;
			requestRoute();
		});
	}
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setObjectName(QStringLiteral("navigationStatus"));
	m_layout->addWidget(m_status);
	m_layout->addStretch(1);
	m_progress = new QLabel(tr("模拟导航 · 不代表实际位置"), this);
	m_progress->setWordWrap(true);
	m_progress->setObjectName(QStringLiteral("simulationProgress"));
	m_layout->addWidget(m_progress);
	auto *controls = new QHBoxLayout;
	m_play = new QPushButton(tr("开始模拟"), this);
	m_play->setObjectName(QStringLiteral("playSimulation"));
	m_restart = new QPushButton(tr("重新开始"), this);
	m_restart->setObjectName(QStringLiteral("restartSimulation"));
	controls->addWidget(m_play);
	controls->addWidget(m_restart);
	controls->addWidget(new QLabel(tr("30 倍速演示"), this));
	controls->addStretch();
	m_layout->addLayout(controls);
	m_steps = new QListWidget(this);
	m_steps->setObjectName(QStringLiteral("routeSteps"));
	m_steps->setMaximumHeight(125);
	m_layout->addWidget(m_steps);
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, [this] {
		m_playback.advance(m_elapsed.restart() / 1000.0 * 30);
		if (m_playback.finished())
			pause();
		updatePlayback();
	});
	connect(m_play, &QPushButton::clicked, this, [this] {
		if (m_timer->isActive()) {
			pause();
		} else if (m_playback.ready() && !m_playback.finished()) {
			m_elapsed.start();
			m_timer->start();
			m_play->setText(tr("暂停"));
		}
	});
	connect(m_restart, &QPushButton::clicked, this, [this] {
		pause();
		m_playback.reset();
		updatePlayback();
	});
	connect(back, &QPushButton::clicked, this, &NavigationView::backRequested);
	updatePlayback();
}

void NavigationView::open(const Station &station) {
	m_station = station;
	m_title->setText(station.name);
	requestRoute();
}

void NavigationView::hideEvent(QHideEvent *event) {
	pause();
	QWidget::hideEvent(event);
}

void NavigationView::pause() {
	m_timer->stop();
	m_play->setText(tr("开始模拟"));
}

void NavigationView::requestRoute() {
	pause();
	m_playback = RoutePlayback{};
	m_steps->clear();
	m_route = {};
	m_mapReady = false;
#ifdef USER_APP_HAS_WEBENGINE
	if (m_webView) {
		m_webView->stop();
		m_webView->hide();
	}
#endif
	updatePlayback();
	m_status->setText(tr("正在规划路线…"));
	const auto requestId = ++m_requestId;
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("fromLatitude"), QString::number(m_session.latitude(), 'f', 6));
	query.addQueryItem(QStringLiteral("fromLongitude"), QString::number(m_session.longitude(), 'f', 6));
	query.addQueryItem(QStringLiteral("toLatitude"), QString::number(m_station.latitude, 'f', 6));
	query.addQueryItem(QStringLiteral("toLongitude"), QString::number(m_station.longitude, 'f', 6));
	query.addQueryItem(QStringLiteral("mode"), m_mode);
	const QPointer<NavigationView> guard(this);
	m_api.get(QStringLiteral("/locations/routes?%1").arg(query.toString(QUrl::FullyEncoded)), [this, guard, requestId](const QJsonValue &data, const QJsonObject &) {
			if (!guard || requestId != m_requestId)
				return;
			const auto route = data.toObject();
			if (!m_playback.load(route)) {
				m_status->setText(tr("路线数据无效，请返回后重新选择位置。"));
				return;
			}
			m_steps->addItems(m_playback.instructions());
			if (m_playback.instructions().isEmpty())
				m_steps->addItem(tr("地图服务未提供分段指引，请参考路线。"));
			showMap(route);
			updatePlayback(); }, [this, guard, requestId](const ApiError &error) {
			if (!guard || requestId != m_requestId)
				return;
			m_status->setText(error.message.isEmpty() ? error.code : error.message); });
}

void NavigationView::showMap(const QJsonObject &route) {
	m_route = route;
#ifdef USER_APP_HAS_WEBENGINE
	if (!m_webView) {
		m_webView = new QWebEngineView(this);
		m_webView->setObjectName(QStringLiteral("routeMap"));
		m_webView->setPage(new RoutePage(m_webView));
		m_webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
		connect(m_webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
			if (!ok || m_route.isEmpty()) {
				if (!ok)
					m_status->setText(tr("路线地图加载失败，请返回后重试。"));
				return;
			}
			QJsonObject config{{QStringLiteral("route"), m_route},
							   {QStringLiteral("key"), qEnvironmentVariable("TENCENT_MAP_KEY")}};
			const auto json = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
			m_mapReady = true;
			m_webView->page()->runJavaScript(QStringLiteral("showRoute(%1)").arg(json));
			updatePlayback();
		});
		delete m_layout->takeAt(2);
		m_layout->insertWidget(2, m_webView, 1);
	}
	m_status->clear();
	m_webView->show();
	m_webView->load(QUrl(QStringLiteral("qrc:/navigation/map.html")));
#else
	m_status->setText(tr("当前环境缺少 Qt WebEngine，无法显示地图；可查看分段指引和模拟进度。"));
#endif
}

void NavigationView::updatePlayback() {
	m_play->setEnabled(m_playback.ready() && !m_playback.finished());
	m_restart->setEnabled(m_playback.ready());
	if (!m_playback.ready()) {
		m_progress->setText(tr("模拟导航 · 不代表实际位置"));
		return;
	}
	m_progress->setText(m_playback.finished() ? tr("模拟导航 · 已到达目标充电站") : tr("模拟导航 · 剩余 %1 公里 · 不代表实际位置").arg(m_playback.remainingMeters() / 1000, 0, 'f', 2));
	m_steps->setCurrentRow(m_playback.stepIndex());
#ifdef USER_APP_HAS_WEBENGINE
	if (m_mapReady) {
		const auto point = m_playback.position();
		m_webView->page()->runJavaScript(QStringLiteral("setPosition(%1,%2)").arg(point.x(), 0, 'f', 8).arg(point.y(), 0, 'f', 8));
	}
#endif
}
