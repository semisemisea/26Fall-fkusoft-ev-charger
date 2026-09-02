#include "mainwindow.h"
#include "LoginView.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

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
    auto *homePlaceholder = buildHomePlaceholder();
    ui->pages->addWidget(loginView);
    ui->pages->addWidget(homePlaceholder);

    connect(loginView, &LoginView::loginSucceeded, this, [this, homePlaceholder] {
        static_cast<QLabel *>(homePlaceholder->layout()->itemAt(0)->widget())
            ->setText(QStringLiteral("欢迎，%1\n余额：%2 分").arg(
                m_session->user().nickname, QString::number(m_session->user().walletBalanceFen)));
        ui->pages->setCurrentWidget(homePlaceholder);
    });

    ui->pages->setCurrentWidget(loginView);
}

MainWindow::~MainWindow()
{
    delete ui;
}

QWidget *MainWindow::buildHomePlaceholder()
{
    auto *placeholder = new QWidget(this);
    auto *label = new QLabel(QStringLiteral("主页（M2 附近充电站）"), placeholder);
    label->setAlignment(Qt::AlignCenter);
    auto *layout = new QVBoxLayout(placeholder);
    layout->addWidget(label);
    return placeholder;
}
