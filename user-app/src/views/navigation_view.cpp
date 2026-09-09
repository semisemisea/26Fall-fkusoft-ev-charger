/**
 * @file navigation_view.cpp
 * @brief 请求站点路线并在可用的 Qt WebEngine 环境中显示地图。
 */
#include <evcharger/logging.h>

#include "navigation_view.h"

#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#ifdef USER_APP_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif
#include "widgets/back_button.h"

Q_LOGGING_CATEGORY(userNavigationViewLog, "evcharger.user.ui", QtInfoMsg)

namespace {
	// 检测 QtWebEngineProcess 是否存在，用于判断当前环境能否加载地图
	/**
	 * @brief 检查是否编入 WebEngine 且辅助进程文件存在。
	 * @return 编入模块且运行时辅助进程存在时为 true；不保证地图网络可达。
	 */
	bool webEngineAvailable() {
#ifdef USER_APP_HAS_WEBENGINE
		const QString dir = QLibraryInfo::path(QLibraryInfo::LibraryExecutablesPath);
		return QFile::exists(dir + QStringLiteral("/QtWebEngineProcess")) || QFile::exists(dir + QStringLiteral("/QtWebEngineProcess.exe"));
#else
		return false;
#endif
	}

#ifdef USER_APP_HAS_WEBENGINE
	// 自定义地图页面：新窗口在当前页打开，且拒绝协议名不以 http 开头的导航
	/**
	 * @brief 复用当前 WebEngine 页处理弹窗，并过滤导航协议。
	 * @details 当前实现接受 scheme 以 http 开头的 URL，其余协议拒绝；并非精确的 http/https 白名单。
	 */
	class MapPage : public QWebEnginePage {
	public:
		using QWebEnginePage::QWebEnginePage;

	protected:
		/**
		 * @brief 将地图的新窗口请求复用到当前页面。
		 * @param type WebEngine 请求的新窗口类型，本实现不区分类型。
		 * @return 当前页指针，不创建新页面或转移所有权。
		 */
		QWebEnginePage *createWindow(WebWindowType type) override {
			Q_UNUSED(type)
			return this;
		}

		/**
		 * @brief 决定是否允许地图页面发起当前导航。
		 * @param url 请求导航的 URL。
		 * @param type 导航来源类型，转交基类处理。
		 * @param isMainFrame 是否属于主框架，转交基类处理。
		 * @return 协议不以 http 开头时为 false，否则返回基类决策。
		 */
		bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
			if (!url.scheme().startsWith(QLatin1String("http"))) {
				return false;
			}
			return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
		}
	};

	// 注入的触屏桥接脚本：把鼠标按下 / 抬起转换为 touchstart / touchend，让桌面鼠标也能拖动移动端地图
	/// @brief 地图加载成功后注入的鼠标按下/抬起到触摸事件桥接脚本；未合成 touchmove。
	const char kTouchBridgeScript[] = R"JS(
(function(){
  function fire(type, e){
    try{
      var t = new Touch({identifier: 1, target: e.target, clientX: e.clientX, clientY: e.clientY,
                         pageX: e.pageX, pageY: e.pageY});
      e.target.dispatchEvent(new TouchEvent(type, {bubbles: true, cancelable: true,
        touches: type === 'touchend' ? [] : [t],
        targetTouches: type === 'touchend' ? [] : [t],
        changedTouches: [t]}));
    }catch(err){}
  }
  window.addEventListener('mousedown', function(e){ fire('touchstart', e); }, true);
  window.addEventListener('mouseup', function(e){ fire('touchend', e); }, true);
})();
)JS";
#endif
} // namespace

/**
 * @details 构造：搭建标题栏与状态提示区；出行方式由地图页面提供切换
 */
NavigationView::NavigationView(Session &session, ApiClient &api, QWidget *parent)
	: QWidget(parent), m_session(session), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("NavigationView"));
	EV_LOG_DEBUG(userNavigationViewLog, this) << "View initialized";
	auto *backButton = new BackButton(this);
	auto *titleLabel = new QLabel(this);
	titleLabel->setObjectName(QStringLiteral("pageTitle"));
	titleLabel->setObjectName(QStringLiteral("stationTitle"));

	m_statusLabel = new QLabel(this);
	m_statusLabel->setObjectName(QStringLiteral("error"));
	m_statusLabel->hide();

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(12, 12, 12, 12);

	auto *headerRow = new QHBoxLayout;
	headerRow->addWidget(backButton);
	headerRow->addStretch();
	headerRow->addWidget(titleLabel);
	headerRow->addStretch();
	m_layout->addLayout(headerRow);

	m_layout->addWidget(m_statusLabel);
	m_layout->addStretch();

	connect(backButton, &QPushButton::clicked, this, &NavigationView::backRequested);
}

/**
 * @details 以目标站点打开本页：重置地图并立即规划路线
 */
void NavigationView::open(const Station &station) {
	EV_LOG_INFO(userNavigationViewLog, this) << "Opening navigation";
	m_station = station;
	findChild<QLabel *>(QStringLiteral("stationTitle"))->setText(station.name);
	m_statusLabel->hide();
#ifdef USER_APP_HAS_WEBENGINE
	if (m_webView) {
		m_webView->stop();
		m_webView->hide();
	}
#endif
	requestRoute();
}

/**
 * @details 请求路线规划：首次成功时创建 WebEngine 视图并加载地图链接
 */
void NavigationView::requestRoute() {
	EV_LOG_INFO(userNavigationViewLog, this) << "Requesting route";
	m_statusLabel->setText(QStringLiteral("正在规划路线..."));
	m_statusLabel->show();

	const auto requestId = ++m_requestId;
	QUrlQuery query;
	query.addQueryItem(QLatin1String("fromLatitude"), QString::number(m_session.latitude()));
	query.addQueryItem(QLatin1String("fromLongitude"), QString::number(m_session.longitude()));
	query.addQueryItem(QLatin1String("toLatitude"), QString::number(m_station.latitude));
	query.addQueryItem(QLatin1String("toLongitude"), QString::number(m_station.longitude));
	query.addQueryItem(QLatin1String("mode"), QStringLiteral("driving"));
	query.addQueryItem(QLatin1String("fromName"), QStringLiteral("我的位置"));
	query.addQueryItem(QLatin1String("toName"), m_station.name);

	m_api.get(QStringLiteral("/locations/routes?%1").arg(query.toString(QUrl::FullyEncoded)), [this, requestId](const QJsonValue &data, const QJsonObject &) {
		if (requestId != m_requestId)
			return;
		const QJsonObject object = data.toObject();
		m_statusLabel->hide();

		const QUrl mapUrl(object.value(QLatin1String("mapUrl")).toString());
		if (!mapUrl.isValid()) {
			EV_LOG_WARNING(userNavigationViewLog, this) << "Route returned an invalid map URL";
			return;
		}
		if (!webEngineAvailable()) {
			EV_LOG_WARNING(userNavigationViewLog, this) << "Map unavailable: WebEngine runtime missing";
			m_statusLabel->setText(QStringLiteral("当前环境缺少 Qt WebEngine 运行时，无法加载地图"));
			m_statusLabel->show();
			return;
		}
#ifdef USER_APP_HAS_WEBENGINE
		if (!m_webView) {
			m_webView = new QWebEngineView(this);
			auto *mapPage = new MapPage(QWebEngineProfile::defaultProfile(), m_webView);
			m_webView->setPage(mapPage);
			m_webView->page()->profile()->setHttpUserAgent(QStringLiteral(
				"Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"));
			connect(m_webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
				if (ok) {
					EV_LOG_INFO(userNavigationViewLog, this) << "Map loaded";
				} else {
					EV_LOG_WARNING(userNavigationViewLog, this) << "Map load failed";
				}
				if (ok && m_webView) {
					m_webView->page()->runJavaScript(QString::fromUtf8(kTouchBridgeScript));
				}
			});
			delete m_layout->takeAt(m_layout->count() - 1);
			m_layout->addWidget(m_webView, 1);
		}
		m_webView->show();
		m_webView->load(mapUrl);
#endif
	},
			  [this, requestId](const ApiError &error) {
                  if (requestId != m_requestId)
                      return;
 EV_LOG_WARNING(userNavigationViewLog, this) << "API operation failed in view";
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}
