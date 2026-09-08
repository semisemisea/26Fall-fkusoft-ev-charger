/**
 * @file mainwindow.cpp
 * @brief 装配用户前端依赖并统一协调登录、主标签页和覆盖页导航。
 */
#include <evcharger/logging.h>

#include "ChargingTab.h"
#include "InfoPages.h"
#include "LoginView.h"
#include "NavigationView.h"
#include "ProfileView.h"
#include "StationDetailView.h"
#include "StationListView.h"
#include "common/Demo.h"
#include "common/Theme.h"
#include "mainwindow.h"
#include "models/Order.h"
#include "models/Reservation.h"
#include "ui_mainwindow.h"

#include "widgets/AppIcons.h"
#include "widgets/Toast.h"
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(userWindowLog, "evcharger.user.ui", QtInfoMsg)

// 手机隐喻视口：固定 390x780
namespace {
	/// @brief 手机式演示视口宽度，单位像素。
	constexpr int kPhoneWidth = 390;
	/// @brief 手机式演示视口高度，单位像素。
	constexpr int kPhoneHeight = 780;
	// 演示后端地址（本地 mock server，契约见 docs/apis.md）
	/// @brief 本地服务默认地址，包含 /api/v1 API 前缀。
	const QLatin1String kDefaultBaseUrl{"http://localhost:8080/api/v1"};
} // namespace

/**
 * @details 构造函数：固定手机视口尺寸，装配状态栏/Tab/全部页面，并统一编排页面跳转信号
 */
MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent), ui(new Ui::MainWindow), m_session(new Session(this)), m_api(new ApiClient(kDefaultBaseUrl, this)) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("MainWindow"));
	EV_LOG_DEBUG(userWindowLog, this) << "View initialized";
	ui->setupUi(this);
	setFixedSize(kPhoneWidth, kPhoneHeight);

	buildStatusBar();
	buildTabBar();

	// 登录态与网络层联动：登录注入令牌，退出清空令牌并回到登录页
	connect(m_session, &Session::signedIn, this, [this] { m_api->setAccessToken(m_session->accessToken()); });
	connect(m_session, &Session::signedOut, this, [this] {
		m_api->setAccessToken(QString());
		m_hasActiveOrder = false;
		updateTabIcons();
		ui->tabBar->hide();
		ui->pages->setCurrentWidget(ui->pages->widget(0));
	});

	// 手动 new 出所有页面
	auto *loginView = new LoginView(*m_session, *m_api, this);
	m_stationListView = new StationListView(*m_session, *m_api, this);		 // 找桩首页
	m_chargingTab = new ChargingTab(*m_session, *m_api, this);				 // 充电页
	m_profileView = new ProfileView(*m_session, *m_api, this);				 // 个人中心页
	m_stationDetailView = new StationDetailView(*m_api, this);				 // 电站详情页
	m_navigationView = new NavigationView(*m_session, *m_api, this);		 // 导航页
	auto *orderHistoryView = new OrderHistoryView(*m_api, this);			 // 订单历史页
	auto *reservationHistoryView = new ReservationHistoryView(*m_api, this); // 预约记录页
	auto *transactionsView = new TransactionsView(*m_api, this);			 // 钱包流水
	auto *aboutView = new AboutView(this);									 // 关于页

	// QStackedWidget 作为 Tab 页容器，找桩/充电/我的三页在其中切换
	m_tabContainer = new QWidget(this);
	m_tabStack = new QStackedWidget(m_tabContainer);
	m_tabStack->addWidget(m_stationListView);
	m_tabStack->addWidget(m_chargingTab);
	m_tabStack->addWidget(m_profileView);
	auto *tabLayout = new QVBoxLayout(m_tabContainer);
	tabLayout->setContentsMargins(0, 0, 0, 0);
	tabLayout->addWidget(m_tabStack);

	// 将所有页面加入ui，登录页在最前，Tab 容器在中间，覆盖页在后
	ui->pages->addWidget(loginView);
	ui->pages->addWidget(m_tabContainer);
	ui->pages->addWidget(m_stationDetailView);
	ui->pages->addWidget(m_navigationView);
	ui->pages->addWidget(orderHistoryView);
	ui->pages->addWidget(reservationHistoryView);
	ui->pages->addWidget(transactionsView);
	ui->pages->addWidget(aboutView);

	// 各页面跳转信号连接
	// 登录页登录成功后切到找桩页
	connect(loginView, &LoginView::loginSucceeded, this, [this] { showTab(0); });

	// 找桩页
	connect(m_stationListView, &StationListView::stationSelected, this, [this](const Station &station) {
		m_stationDetailView->open(station);
		enterOverlay(m_stationDetailView);
	});
	connect(m_stationListView, &StationListView::navigateRequested, this,
			[this](const Station &station) { navigateTo(station, m_tabContainer); });
	// 站点详情页
	connect(m_stationDetailView, &StationDetailView::backRequested, this, [this] { showTab(0); });
	connect(m_stationDetailView, &StationDetailView::navigateRequested, this,
			[this](const Station &station) { navigateTo(station, m_stationDetailView); });
	connect(m_stationDetailView, &StationDetailView::chargeRequested, this,
			&MainWindow::startChargingFromDetail);
	connect(m_stationDetailView, &StationDetailView::reservationRequested, this,
			&MainWindow::handleReservationFromDetail);

	// 充电页
	connect(m_chargingTab, &ChargingTab::orderSettled, this, &MainWindow::refreshBalance);
	connect(m_chargingTab, &ChargingTab::returnHomeRequested, this, [this] { showTab(0); });
	connect(m_chargingTab, &ChargingTab::activeOrderChanged, this, [this](bool hasActive) {
		m_hasActiveOrder = hasActive;
		updateTabIcons();
	});

	// 个人中心页
	connect(m_profileView, &ProfileView::ordersRequested, this,
			[this, orderHistoryView] { enterOverlay(orderHistoryView); });
	connect(m_profileView, &ProfileView::reservationsRequested, this,
			[this, reservationHistoryView] { enterOverlay(reservationHistoryView); });
	connect(m_profileView, &ProfileView::transactionsRequested, this,
			[this, transactionsView] { enterOverlay(transactionsView); });
	connect(m_profileView, &ProfileView::aboutRequested, this, [this, aboutView] { enterOverlay(aboutView); });
	// 各覆盖页的返回按钮
	connect(orderHistoryView, &OrderHistoryView::backRequested, this, [this] { showTab(2); });
	connect(reservationHistoryView, &ReservationHistoryView::backRequested, this, [this] { showTab(2); });
	connect(transactionsView, &TransactionsView::backRequested, this, [this] { showTab(2); });
	connect(aboutView, &AboutView::backRequested, this, [this] { showTab(2); });

	connect(m_navigationView, &NavigationView::backRequested, this, [this] {
		if (m_navigationReturnPage == m_stationDetailView) {
			enterOverlay(m_stationDetailView);
		} else {
			showTab(0);
		}
	});

	ui->tabBar->hide();
	ui->pages->setCurrentWidget(loginView);
}

/**
 * @details 析构：仅释放 Designer 生成的 ui 对象，其余控件由 Qt 父子树管理
 */
MainWindow::~MainWindow() {
	delete ui;
}

/**
 * @details 构建顶部状态栏：时间每秒刷新，信号/电量为静态示意
 */
void MainWindow::buildStatusBar() {
	m_timeLabel = new QLabel(ui->statusBar);
	m_timeLabel->setObjectName(QStringLiteral("statusTime"));

	// 信号
	auto *signalLabel = new QLabel(ui->statusBar);
	signalLabel->setPixmap(AppIcons::signal(QColor(0x1e, 0x29, 0x3b), 14));
	signalLabel->setAlignment(Qt::AlignCenter);
	// 电池
	auto *batteryIconLabel = new QLabel(ui->statusBar);
	batteryIconLabel->setPixmap(AppIcons::battery(16));
	auto *batteryTextLabel = new QLabel(QStringLiteral("86%"), ui->statusBar);
	batteryTextLabel->setObjectName(QStringLiteral("statusGlyph"));

	// 水平布局：时间在左，信号/电量在右
	auto *layout = new QHBoxLayout(ui->statusBar);
	layout->setContentsMargins(16, 4, 16, 4);
	layout->addWidget(m_timeLabel);
	layout->addStretch();
	layout->addWidget(signalLabel);
	layout->addSpacing(8);
	layout->addWidget(batteryIconLabel);
	layout->addSpacing(2);
	layout->addWidget(batteryTextLabel);

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] {
		m_timeLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
	});
	timer->start(1000);
	m_timeLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

/**
 * @details 构建底部胶囊 Tab 栏：互斥按钮组，切到“充电”页时主动检查进行中的订单
 */
void MainWindow::buildTabBar() {
	ui->tabBar->setMaximumHeight(84);
	auto *pill = new QFrame(ui->tabBar); // 胶囊：tabBar 里再放一个 QFrame
	pill->setObjectName(QStringLiteral("tabPill"));
	auto *shadow = new QGraphicsDropShadowEffect(pill);
	shadow->setBlurRadius(30);
	shadow->setColor(theme::shadowInk());
	shadow->setOffset(0, 6);
	pill->setGraphicsEffect(shadow);

	const struct
	{
		const char *text;
	} tabs[] = {
		{"找桩"},
		{"充电"},
		{"我的"},
	};

	auto *group = new QButtonGroup(this);
	group->setExclusive(true); // 同一时刻只能有一个按钮处于"选中"态
	auto *pillLayout = new QHBoxLayout(pill);
	pillLayout->setContentsMargins(14, 2, 14, 26);
	pillLayout->setSpacing(0);

	auto *outerLayout = new QHBoxLayout(ui->tabBar);
	outerLayout->setContentsMargins(12, 2, 12, 12);
	outerLayout->addWidget(pill);

	for (int i = 0; i < 3; ++i) {
		auto *button = new QToolButton(pill);
		button->setObjectName(QStringLiteral("tabBarButton"));
		button->setCheckable(true);
		button->setChecked(i == 0);
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		button->setIconSize(QSize(40, 40));
		button->setText(QString::fromUtf8(tabs[i].text));
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		button->setFixedHeight(66);
		group->addButton(button);
		pillLayout->addWidget(button);
		m_tabButtons.append(button);

		connect(button, &QToolButton::clicked, this, [this, i] {
			showTab(i);
			if (i == 1) {
				m_chargingTab->checkActiveOrder(); // 每次进"充电"Tab 都查一次
			}
		});
	}
	updateTabIcons();
}

/**
 * @details 刷新 Tab 图标：自绘选中态圆底，充电 Tab 在有进行中订单时叠加红点
 */
void MainWindow::updateTabIcons() {
	const QColor active = QColor(0x2B, 0xFF, 0x7D);
	const QColor inactive = theme::textSecondary();

	auto makeTabIcon = [](const QPixmap &source, bool selected) -> QPixmap {
		constexpr int kCanvasSize = 40;
		QPixmap canvas(kCanvasSize, kCanvasSize);
		canvas.fill(Qt::transparent);
		QPainter painter(&canvas);
		painter.setRenderHint(QPainter::Antialiasing);
		if (selected) {
			painter.setBrush(QColor(0x1a, 0x1a, 0x1a));
			painter.setPen(Qt::NoPen);
			constexpr int kCircleSize = 36;
			painter.drawEllipse((kCanvasSize - kCircleSize) / 2, (kCanvasSize - kCircleSize) / 2, kCircleSize, kCircleSize);
		}
		const int x = (kCanvasSize - source.width()) / 2;
		const int y = (kCanvasSize - source.height()) / 2;
		painter.drawPixmap(x, y, source);
		return canvas;
	};

	for (int i = 0; i < m_tabButtons.size(); ++i) {
		const bool hasBadge = (i == 1 && m_hasActiveOrder);
		QPixmap normal;
		QPixmap on;
		switch (i) {
		case 0:
			normal = makeTabIcon(AppIcons::pin(inactive, 24, false), false);
			on = makeTabIcon(AppIcons::pin(active, 24, false), true);
			break;
		case 1:
			normal = makeTabIcon(AppIcons::bolt(inactive, 24, hasBadge), false);
			on = makeTabIcon(AppIcons::bolt(active, 24, hasBadge), true);
			break;
		default:
			normal = makeTabIcon(AppIcons::person(inactive, 24, false), false);
			on = makeTabIcon(AppIcons::person(active, 24, false), true);
			break;
		}
		QIcon icon;
		icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
		icon.addPixmap(on, QIcon::Normal, QIcon::On);
		m_tabButtons.at(i)->setIcon(icon);
	}
}

/**
 * @details 切换主 Tab 并确保显示主内容区与底部 Tab 栏
 */
void MainWindow::showTab(int index) {
	EV_LOG_INFO(userWindowLog, this) << "Switching primary tab" << "tab_index=" << index;
	m_tabStack->setCurrentIndex(index);
	for (int i = 0; i < m_tabButtons.size(); ++i) {
		m_tabButtons.at(i)->setChecked(i == index);
	}
	updateTabIcons();
	ui->pages->setCurrentWidget(m_tabContainer);
	ui->tabBar->show();
}

/**
 * @details 进入覆盖页：隐藏 Tab 栏；导航页跳过淡入，其余页面播放淡入动画后移除效果
 */
void MainWindow::enterOverlay(QWidget *page) {
	EV_LOG_INFO(userWindowLog, this) << "Opening overlay page" << "page_class=" << page->metaObject()->className();
	ui->pages->setCurrentWidget(page);
	ui->tabBar->hide();

	// WebEngine 原生合成视图不套 QWidget 透明度效果，避免地图被覆盖页淡入影响。
	if (page == m_navigationView) {
		return;
	}
	auto *fade = new QGraphicsOpacityEffect(page);
	page->setGraphicsEffect(fade);
	fade->setOpacity(0.0);
	auto *anim = new QPropertyAnimation(fade, "opacity", page);
	anim->setDuration(demo::ms(160));
	anim->setStartValue(0.0);
	anim->setEndValue(1.0);
	anim->setEasingCurve(QEasingCurve::OutCubic);
	connect(anim, &QPropertyAnimation::finished, page, [page] { page->setGraphicsEffect(nullptr); });
	anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @details 打开导航页并记录返回目标（详情页或主页），供返回按钮使用
 */
void MainWindow::navigateTo(const Station &station, QWidget *returnPage) {
	EV_LOG_INFO(userWindowLog, this) << "Opening station navigation";
	m_navigationReturnPage = returnPage;
	m_navigationView->open(station);
	enterOverlay(m_navigationView);
}

/**
 * @details 从详情页立即充电：确认后 POST /orders；已有进行中订单时直接切到充电页
 */
void MainWindow::startChargingFromDetail(const Charger &charger) {
	EV_LOG_INFO(userWindowLog, this) << "Charge selection requested";
	const auto choice = QMessageBox::question(this, QStringLiteral("选择电桩"),
											  QStringLiteral("是否选择电桩 %1 立即充电？").arg(charger.code));
	if (choice != QMessageBox::Yes) {
		return;
	}

	QJsonObject orderBody;
	orderBody.insert(QLatin1String("chargerId"), charger.id);
	m_api->post(QStringLiteral("/orders"), orderBody, [this](const QJsonValue &orderData, const QJsonObject &) {
                    m_chargingTab->showCharging(Order::fromJson(orderData.toObject()));
                    showTab(1); }, [this](const ApiError &error) {
 EV_LOG_WARNING(userWindowLog, this) << "API operation failed in view";
                    if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                        showTab(1);
                        m_chargingTab->checkActiveOrder();
                        return;
                    }
                    Toast::error(this, error.message.isEmpty() ? error.code : error.message); });
}

/**
 * @details 从详情页预约电桩：确认后 POST /reservations（保留 15 分钟），处理已有订单冲突
 */
void MainWindow::handleReservationFromDetail(const Charger &charger) {
	EV_LOG_INFO(userWindowLog, this) << "Reservation selection requested";
	const auto choice = QMessageBox::question(this, QStringLiteral("预约电桩"),
											  QStringLiteral("是否预约电桩 %1？保留 15 分钟。").arg(charger.code));
	if (choice != QMessageBox::Yes) {
		return;
	}

	QJsonObject reservationBody;
	reservationBody.insert(QLatin1String("chargerId"), charger.id);
	reservationBody.insert(QLatin1String("holdMinutes"), 15);
	m_api->post(QStringLiteral("/reservations"), reservationBody, [this](const QJsonValue &data, const QJsonObject &) {
                    m_chargingTab->showReservation(Reservation::fromJson(data.toObject()));
                    showTab(1); }, [this](const ApiError &error) {
 EV_LOG_WARNING(userWindowLog, this) << "API operation failed in view";
                    if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                        showTab(1);
                        m_chargingTab->checkActiveOrder();
                        return;
                    }
                    Toast::error(this, error.message.isEmpty() ? error.code : error.message); });
}

/**
 * @details 结算完成后拉取 GET /me 刷新 Session 中的余额（失败静默忽略）
 */
void MainWindow::refreshBalance() {
	EV_LOG_INFO(userWindowLog, this) << "Refreshing wallet balance";
	m_api->get(QStringLiteral("/me"), [this](const QJsonValue &data, const QJsonObject &) { m_session->updateBalance(data.toObject().value(QLatin1String("walletBalanceFen")).toInteger()); }, [](const ApiError &) { EV_LOG_WARNING(userWindowLog, nullptr) << "Background view refresh failed"; });
}
