#include "mainwindow.h"
#include "ChargingTab.h"
#include "InfoPages.h"
#include "LoginView.h"
#include "NavigationView.h"
#include "ProfileView.h"
#include "StationDetailView.h"
#include "StationListView.h"
#include "ui_mainwindow.h"
#include "common/Demo.h"
#include "common/Theme.h"
#include "models/Order.h"
#include "models/Reservation.h"

#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QJsonObject>
#include "widgets/AppIcons.h"
#include "widgets/Toast.h"

namespace {
constexpr int kPhoneWidth = 390;
constexpr int kPhoneHeight = 780;
const QLatin1String kDefaultBaseUrl{"http://localhost:8080/api/v1"};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_session(new Session(this))
    , m_api(new ApiClient(kDefaultBaseUrl, this))
{
    ui->setupUi(this);
    setFixedSize(kPhoneWidth, kPhoneHeight);

    buildStatusBar();
    buildTabBar();

    connect(m_session, &Session::signedIn, this, [this] { m_api->setAccessToken(m_session->accessToken()); });
    connect(m_session, &Session::signedOut, this, [this] {
        m_api->setAccessToken(QString());
        m_hasActiveOrder = false;
        updateTabIcons();
        ui->tabBar->hide();
        ui->pages->setCurrentWidget(ui->pages->widget(0));
    });

    auto *loginView = new LoginView(*m_session, *m_api, this);
    m_stationListView = new StationListView(*m_session, *m_api, this);
    m_chargingTab = new ChargingTab(*m_session, *m_api, this);
    m_profileView = new ProfileView(*m_session, *m_api, this);
    m_stationDetailView = new StationDetailView(*m_api, this);
    m_navigationView = new NavigationView(*m_session, *m_api, this);
    auto *orderHistoryView = new OrderHistoryView(*m_api, this);
    auto *reservationHistoryView = new ReservationHistoryView(*m_api, this);
    auto *transactionsView = new TransactionsView(*m_api, this);
    auto *carView = new CarView(this);
    auto *aboutView = new AboutView(this);

    m_tabContainer = new QWidget(this);
    m_tabStack = new QStackedWidget(m_tabContainer);
    m_tabStack->addWidget(m_stationListView);
    m_tabStack->addWidget(m_chargingTab);
    m_tabStack->addWidget(m_profileView);
    auto *tabLayout = new QVBoxLayout(m_tabContainer);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->addWidget(m_tabStack);

    ui->pages->addWidget(loginView);
    ui->pages->addWidget(m_tabContainer);
    ui->pages->addWidget(m_stationDetailView);
    ui->pages->addWidget(m_navigationView);
    ui->pages->addWidget(orderHistoryView);
    ui->pages->addWidget(reservationHistoryView);
    ui->pages->addWidget(transactionsView);
    ui->pages->addWidget(carView);
    ui->pages->addWidget(aboutView);

    connect(loginView, &LoginView::loginSucceeded, this, [this] { showTab(0); });

    connect(m_stationListView, &StationListView::stationSelected, this, [this](const Station &station) {
        m_stationDetailView->open(station);
        enterOverlay(m_stationDetailView);
    });
    connect(m_stationListView, &StationListView::navigateRequested, this,
            [this](const Station &station) { navigateTo(station, m_tabContainer); });
    connect(m_stationDetailView, &StationDetailView::backRequested, this, [this] { showTab(0); });
    connect(m_stationDetailView, &StationDetailView::navigateRequested, this,
            [this](const Station &station) { navigateTo(station, m_stationDetailView); });
    connect(m_stationDetailView, &StationDetailView::chargeRequested, this,
            &MainWindow::startChargingFromDetail);
    connect(m_stationDetailView, &StationDetailView::reservationRequested, this,
            &MainWindow::handleReservationFromDetail);

    connect(m_chargingTab, &ChargingTab::orderSettled, this, &MainWindow::refreshBalance);
    connect(m_chargingTab, &ChargingTab::returnHomeRequested, this, [this] { showTab(0); });
    connect(m_chargingTab, &ChargingTab::activeOrderChanged, this, [this](bool hasActive) {
        m_hasActiveOrder = hasActive;
        updateTabIcons();
    });

    connect(m_profileView, &ProfileView::ordersRequested, this,
            [this, orderHistoryView] { enterOverlay(orderHistoryView); });
    connect(m_profileView, &ProfileView::reservationsRequested, this,
            [this, reservationHistoryView] { enterOverlay(reservationHistoryView); });
    connect(m_profileView, &ProfileView::transactionsRequested, this,
            [this, transactionsView] { enterOverlay(transactionsView); });
    connect(m_profileView, &ProfileView::carRequested, this, [this, carView] { enterOverlay(carView); });
    connect(m_profileView, &ProfileView::aboutRequested, this, [this, aboutView] { enterOverlay(aboutView); });
    connect(orderHistoryView, &OrderHistoryView::backRequested, this, [this] { showTab(2); });
    connect(reservationHistoryView, &ReservationHistoryView::backRequested, this, [this] { showTab(2); });
    connect(transactionsView, &TransactionsView::backRequested, this, [this] { showTab(2); });
    connect(carView, &CarView::backRequested, this, [this] { showTab(2); });
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

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildStatusBar()
{
    m_timeLabel = new QLabel(ui->statusBar);
    m_timeLabel->setObjectName(QStringLiteral("statusTime"));

    auto *signalLabel = new QLabel(QStringLiteral("📶"), ui->statusBar);
    signalLabel->setObjectName(QStringLiteral("statusGlyph"));
    auto *batteryLabel = new QLabel(QStringLiteral("🔋 86%"), ui->statusBar);
    batteryLabel->setObjectName(QStringLiteral("statusGlyph"));

    auto *layout = new QHBoxLayout(ui->statusBar);
    layout->setContentsMargins(16, 4, 16, 4);
    layout->addWidget(m_timeLabel);
    layout->addStretch();
    layout->addWidget(signalLabel);
    layout->addSpacing(8);
    layout->addWidget(batteryLabel);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        m_timeLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    });
    timer->start(1000);
    m_timeLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

void MainWindow::buildTabBar()
{
    auto *pill = new QFrame(ui->tabBar);
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
    group->setExclusive(true);
    auto *pillLayout = new QHBoxLayout(pill);
    pillLayout->setContentsMargins(8, 4, 8, 4);
    pillLayout->setSpacing(0);

    auto *outerLayout = new QHBoxLayout(ui->tabBar);
    outerLayout->setContentsMargins(12, 2, 12, 10);
    outerLayout->addWidget(pill);

    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(pill);
        button->setObjectName(QStringLiteral("tabBarButton"));
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		button->setIconSize(QSize(32, 32));
        button->setText(QString::fromUtf8(tabs[i].text));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        group->addButton(button);
        pillLayout->addWidget(button);
        m_tabButtons.append(button);

        connect(button, &QToolButton::clicked, this, [this, i] {
            showTab(i);
            if (i == 1) {
                m_chargingTab->checkActiveOrder();
            }
        });
    }
    updateTabIcons();
}

void MainWindow::updateTabIcons()
{
    const QColor active = theme::primary();
    const QColor inactive = theme::textSecondary();

    for (int i = 0; i < m_tabButtons.size(); ++i) {
        const bool hasBadge = (i == 1 && m_hasActiveOrder);
        QPixmap normal;
        QPixmap on;
        switch (i) {
        case 0:
            normal = AppIcons::pin(inactive, 24, false);
            on = AppIcons::pin(active, 24, false);
            break;
        case 1:
            normal = AppIcons::bolt(inactive, 24, hasBadge);
            on = AppIcons::bolt(active, 24, hasBadge);
            break;
        default:
            normal = AppIcons::person(inactive, 24, false);
            on = AppIcons::person(active, 24, false);
            break;
        }
        QIcon icon;
        icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
        icon.addPixmap(on, QIcon::Normal, QIcon::On);
        m_tabButtons.at(i)->setIcon(icon);
    }
}

void MainWindow::showTab(int index)
{
    m_tabStack->setCurrentIndex(index);
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        m_tabButtons.at(i)->setChecked(i == index);
    }
    updateTabIcons();
    ui->pages->setCurrentWidget(m_tabContainer);
    ui->tabBar->show();
}

void MainWindow::enterOverlay(QWidget *page)
{
    ui->pages->setCurrentWidget(page);
    ui->tabBar->hide();

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

void MainWindow::navigateTo(const Station &station, QWidget *returnPage)
{
    m_navigationReturnPage = returnPage;
    m_navigationView->open(station);
    enterOverlay(m_navigationView);
}

void MainWindow::startChargingFromDetail(const Charger &charger)
{
    const auto choice = QMessageBox::question(this, QStringLiteral("选择电桩"),
                                              QStringLiteral("是否选择电桩 %1 立即充电？").arg(charger.code));
    if (choice != QMessageBox::Yes) {
        return;
    }

    QJsonObject orderBody;
    orderBody.insert(QLatin1String("chargerId"), charger.id);
    m_api->post(QStringLiteral("/orders"), orderBody,
                [this](const QJsonValue &orderData, const QJsonObject &) {
                    m_chargingTab->showCharging(Order::fromJson(orderData.toObject()));
                    showTab(1);
                },
                [this](const ApiError &error) {
                    if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                        showTab(1);
                        m_chargingTab->checkActiveOrder();
                        return;
                    }
                    Toast::error(this, error.message.isEmpty() ? error.code : error.message);
                });
}

void MainWindow::handleReservationFromDetail(const Charger &charger)
{
    const auto choice = QMessageBox::question(this, QStringLiteral("预约电桩"),
                                              QStringLiteral("是否预约电桩 %1？保留 15 分钟。").arg(charger.code));
    if (choice != QMessageBox::Yes) {
        return;
    }

    QJsonObject reservationBody;
    reservationBody.insert(QLatin1String("chargerId"), charger.id);
    m_api->post(QStringLiteral("/reservations"), reservationBody,
                [this](const QJsonValue &data, const QJsonObject &) {
                    m_chargingTab->showReservation(Reservation::fromJson(data.toObject()));
                    showTab(1);
                },
                [this](const ApiError &error) {
                    if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                        showTab(1);
                        m_chargingTab->checkActiveOrder();
                        return;
                    }
                    Toast::error(this, error.message.isEmpty() ? error.code : error.message);
                });
}

void MainWindow::refreshBalance()
{
    m_api->get(QStringLiteral("/me"),
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_session->updateBalance(data.toObject().value(QLatin1String("walletBalanceFen")).toInteger());
               },
               [](const ApiError &) {});
}
