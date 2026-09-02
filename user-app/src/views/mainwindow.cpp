#include "mainwindow.h"
#include "LoginView.h"
#include "StationDetailView.h"
#include "StationListView.h"
#include "ui_mainwindow.h"

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

    auto *loginView = new LoginView(*m_session, *m_api, this);
    auto *stationListView = new StationListView(*m_api, this);
    auto *stationDetailView = new StationDetailView(*m_api, this);
    ui->pages->addWidget(loginView);
    ui->pages->addWidget(stationListView);
    ui->pages->addWidget(stationDetailView);

    connect(loginView, &LoginView::loginSucceeded, this, [this, stationListView] {
        ui->pages->setCurrentWidget(stationListView);
    });
    connect(stationListView, &StationListView::stationSelected, this,
            [this, stationDetailView](const Station &station) {
                stationDetailView->open(station);
                ui->pages->setCurrentWidget(stationDetailView);
            });
    connect(stationDetailView, &StationDetailView::backRequested, this, [this, stationListView] {
        ui->pages->setCurrentWidget(stationListView);
    });

    ui->pages->setCurrentWidget(loginView);
}

MainWindow::~MainWindow()
{
    delete ui;
}
