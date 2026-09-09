/**
 * @file navigation_view.cpp
 * @brief 请求站点路线并在可用的 Qt WebEngine 环境中显示地图。
 */
#include <evcharger/logging.h>

#include "navigation_view.h"

#include "widgets/combo_box.h"
#include <QComboBox>
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
	/**
	 * @brief 将导航方式的中文标签映射到接口参数。
	 */
	struct ModeOption {
		const char *label; ///< 供用户选择的 UTF-8 中文标签。
		const char *value; ///< routes 接口接受的 driving 或 walking 值。
	};

	/// @brief 界面提供的驾车和步行模式，标签和接口值保持配对。
	const ModeOption kModeOptions[] = {
		{"驾车", "driving"},
		{"步行", "walking"},
	};

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
 * @details 构造：搭建标题栏、出行方式选择与提示区；切换出行方式时若已加载过则自动重新规划
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

	m_modeCombo = new ComboBox(this);
	for (const ModeOption &option : kModeOptions) {
		m_modeCombo->addItem(QString::fromUtf8(option.label), QLatin1String(option.value));
	}

	m_navigateButton = new QPushButton(QStringLiteral("开始导航"), this);
	m_navigateButton->setObjectName(QStringLiteral("compactButton"));

	m_summaryLabel = new QLabel(this);
	m_summaryLabel->setObjectName(QStringLiteral("routeInfo"));
	m_summaryLabel->hide();

	m_statusLabel = new QLabel(this);
	m_statusLabel->setObjectName(QStringLiteral("error"));
	m_statusLabel->hide();

	m_hintLabel = new QLabel(QStringLiteral("点击「开始导航」加载路线地图"), this);
	m_hintLabel->setAlignment(Qt::AlignCenter);
	m_hintLabel->setObjectName(QStringLiteral("muted"));

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(12, 12, 12, 12);

	auto *headerRow = new QHBoxLayout;
	headerRow->addWidget(backButton);
	headerRow->addStretch();
	headerRow->addWidget(titleLabel);
	headerRow->addStretch();
	m_layout->addLayout(headerRow);

	auto *modeRow = new QHBoxLayout;
	modeRow->addWidget(new QLabel(QStringLiteral("出行方式："), this));
	modeRow->addWidget(m_modeCombo);
	modeRow->addWidget(m_navigateButton);
	modeRow->addStretch();
	m_layout->addLayout(modeRow);
	m_layout->addWidget(m_summaryLabel);
	m_layout->addWidget(m_statusLabel);
	m_layout->addWidget(m_hintLabel, 1);

	connect(backButton, &QPushButton::clicked, this, &NavigationView::backRequested);
	connect(m_navigateButton, &QPushButton::clicked, this, &NavigationView::requestRoute);
	connect(m_modeCombo, &QComboBox::activated, this, [this] {
		if (m_loaded) {
			requestRoute();
		}
	});
}

/**
 * @details 以目标站点打开本页：重置路线信息与地图，等待用户点击“开始导航”
 */
void NavigationView::open(const Station &station) {
	EV_LOG_INFO(userNavigationViewLog, this) << "Opening navigation";
	m_station = station;
	findChild<QLabel *>(QStringLiteral("stationTitle"))->setText(station.name);
	m_summaryLabel->hide();
	m_statusLabel->hide();
	m_loaded = false;
	m_navigateButton->setText(QStringLiteral("开始导航"));
#ifdef USER_APP_HAS_WEBENGINE
	if (m_webView) {
		m_webView->hide();
	}
#endif
	m_hintLabel->show();
}

/**
 * @details 请求路线规划：显示距离 / 时长摘要，首次成功时创建 WebEngine 视图并加载地图链接
 */
void NavigationView::requestRoute() {
	EV_LOG_INFO(userNavigationViewLog, this) << "Requesting route";
	m_navigateButton->setEnabled(false);
	m_statusLabel->setText(QStringLiteral("正在规划路线..."));
	m_statusLabel->show();

	QUrlQuery query;
	query.addQueryItem(QLatin1String("fromLatitude"), QString::number(m_session.latitude()));
	query.addQueryItem(QLatin1String("fromLongitude"), QString::number(m_session.longitude()));
	query.addQueryItem(QLatin1String("toLatitude"), QString::number(m_station.latitude));
	query.addQueryItem(QLatin1String("toLongitude"), QString::number(m_station.longitude));
	query.addQueryItem(QLatin1String("mode"), m_modeCombo->currentData().toString());
	query.addQueryItem(QLatin1String("fromName"), QStringLiteral("我的位置"));
	query.addQueryItem(QLatin1String("toName"), m_station.name);

	m_api.get(QStringLiteral("/locations/routes?%1").arg(query.toString(QUrl::FullyEncoded)), [this](const QJsonValue &data, const QJsonObject &) {
		m_navigateButton->setEnabled(true);
		const QJsonObject object = data.toObject();
		const double distanceKm = object.value(QLatin1String("distanceM")).toInt() / 1000.0;
		const int minutes = qRound(object.value(QLatin1String("durationSec")).toInt() / 60.0);
		m_summaryLabel->setText(QStringLiteral("全程约 %1 公里 · 约 %2 分钟（%3）")
									.arg(distanceKm, 0, 'f', 1)
									.arg(minutes)
									.arg(m_modeCombo->currentText()));
		m_summaryLabel->show();
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
			m_navigateButton->setEnabled(true);
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
			m_layout->addWidget(m_webView, 1);
		}
		m_hintLabel->hide();
		m_webView->show();
		m_webView->load(mapUrl);
		m_loaded = true;
		m_navigateButton->setText(QStringLiteral("重新规划"));
#endif
	},
			  [this](const ApiError &error) {
 EV_LOG_WARNING(userNavigationViewLog, this) << "API operation failed in view";
                  m_navigateButton->setEnabled(true);
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show(); });
}
