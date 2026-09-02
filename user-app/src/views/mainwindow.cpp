#include "mainwindow.h"
#include "ChargingView.h"
#include "LoginView.h"
#include "SettleView.h"
#include "StationDetailView.h"
#include "StationListView.h"
#include "ui_mainwindow.h"
#include "models/Order.h"

#include <QMessageBox>
#include <QJsonObject>

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

    connect(m_session, &Session::signedIn, this, [this] { m_api->setAccessToken(m_session->accessToken()); });
    connect(m_session, &Session::signedOut, this, [this] { m_api->setAccessToken(QString()); });

    auto *loginView = new LoginView(*m_session, *m_api, this);
    auto *stationListView = new StationListView(*m_session, *m_api, this);
    auto *stationDetailView = new StationDetailView(*m_api, this);
    m_chargingView = new ChargingView(*m_api, this);
    m_settleView = new SettleView(*m_api, this);
    ui->pages->addWidget(loginView);
    ui->pages->addWidget(stationListView);
    ui->pages->addWidget(stationDetailView);
    ui->pages->addWidget(m_chargingView);
    ui->pages->addWidget(m_settleView);

    connect(loginView, &LoginView::loginSucceeded, this, [this, stationListView] {
        ui->pages->setCurrentWidget(stationListView);
    });
    connect(stationListView, &StationListView::stationSelected, this,
            [this, stationDetailView](const Station &station) {
                stationDetailView->open(station);
                ui->pages->setCurrentWidget(stationDetailView);
            });
    connect(stationListView, &StationListView::checkActiveOrderRequested, this, &MainWindow::checkActiveOrder);
    connect(stationDetailView, &StationDetailView::backRequested, this, [this, stationListView] {
        ui->pages->setCurrentWidget(stationListView);
    });
    connect(stationDetailView, &StationDetailView::chargeRequested, this,
            [this, stationDetailView](const Charger &charger) {
                QJsonObject reservationBody;
                reservationBody.insert(QLatin1String("chargerId"), charger.id);
                m_api->post(QStringLiteral("/reservations"), reservationBody,
                            [this, charger, stationDetailView](const QJsonValue &data, const QJsonObject &) {
                                QJsonObject orderBody;
                                orderBody.insert(QLatin1String("chargerId"), charger.id);
                                orderBody.insert(QLatin1String("reservationId"),
                                                 data.toObject().value(QLatin1String("id")).toInt());
                                m_api->post(QStringLiteral("/orders"), orderBody,
                                            [this, stationDetailView](const QJsonValue &orderData, const QJsonObject &) {
                                                m_chargingView->open(Order::fromJson(orderData.toObject()));
                                                ui->pages->setCurrentWidget(m_chargingView);
                                            },
                                            [this](const ApiError &error) {
                                                if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                                                    checkActiveOrder();
                                                    return;
                                                }
                                                QMessageBox::warning(this, QStringLiteral("开始充电失败"),
                                                                     error.message.isEmpty() ? error.code : error.message);
                                            });
                            },
                            [this](const ApiError &error) {
                                if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                                    checkActiveOrder();
                                    return;
                                }
                                QMessageBox::warning(this, QStringLiteral("预约失败"),
                                                     error.message.isEmpty() ? error.code : error.message);
                            });
            });
    connect(m_chargingView, &ChargingView::orderStopped, this, [this](const Order &order) {
        m_settleView->open(order);
        ui->pages->setCurrentWidget(m_settleView);
    });
    connect(m_settleView, &SettleView::settled, this, [this, stationListView] {
        m_api->get(QStringLiteral("/me"),
                   [this](const QJsonValue &data, const QJsonObject &) {
                       const QJsonObject object = data.toObject();
                       m_session->updateBalance(object.value(QLatin1String("walletBalanceFen")).toInteger());
                   },
                   [](const ApiError &) {});
        ui->pages->setCurrentWidget(stationListView);
    });
    connect(m_settleView, &SettleView::dismissed, this, [this, stationListView] {
        ui->pages->setCurrentWidget(stationListView);
    });

    ui->pages->setCurrentWidget(loginView);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::checkActiveOrder()
{
    m_api->get(QStringLiteral("/me/active-order"),
               [this](const QJsonValue &data, const QJsonObject &) {
                   if (data.isNull()) {
                       QMessageBox::information(this, QStringLiteral("进行中订单"),
                                                QStringLiteral("暂无进行中的充电订单"));
                       return;
                   }
                   const Order order = Order::fromJson(data.toObject());
                   if (order.status == QLatin1String("charging")) {
                       m_chargingView->open(order);
                       ui->pages->setCurrentWidget(m_chargingView);
                   } else {
                       m_settleView->open(order);
                       ui->pages->setCurrentWidget(m_settleView);
                   }
               },
               [this](const ApiError &error) {
                   QMessageBox::warning(this, QStringLiteral("查询失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}
