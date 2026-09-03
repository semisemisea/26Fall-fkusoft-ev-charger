#include "mainwindow.h"

#include "pages/chargermanagepage.h"
#include "pages/chargerstatuspage.h"
#include "pages/salespage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace {

	// 页面索引;每页一个独立 QWidget,由 pages_*.cpp 实现
	enum PageIndex {
		PageSales = 0,
		PageChargerStatus,
		PageChargerManage,
		PageStationManage,
		PageUserManage,
		PageCount
	};

} // namespace

MainWindow::MainWindow(ops::ApiClient *api, QWidget *parent)
	: QMainWindow(parent), m_api(api) {
	setWindowTitle(tr("充电桩运营管理平台 - 管理后台"));
	resize(1180, 760);

	auto *central = new QWidget(this);
	auto *layout = new QHBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	setCentralWidget(central);

	buildSidebar();

	m_stack = new QStackedWidget(central);
	layout->addWidget(m_stack, 1);
	for (int i = 0; i < PageCount; ++i)
		m_stack->addWidget(createPage(i));

	connect(m_navList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
	m_navList->setCurrentRow(PageSales);

	statusBar()->showMessage(tr("就绪"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildSidebar() {
	auto *side = new QWidget(this);
	side->setObjectName(QStringLiteral("sidebar"));
	side->setFixedWidth(210);
	auto *sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(12, 24, 12, 16);
	sideLayout->setSpacing(8);

	auto *brand = new QLabel(tr("充电桩管理平台"), side);
	brand->setObjectName(QStringLiteral("brand"));
	sideLayout->addWidget(brand);
	sideLayout->addSpacing(16);

	m_navList = new QListWidget(side);
	m_navList->setObjectName(QStringLiteral("navList"));
	m_navList->setFrameShape(QFrame::NoFrame);
	m_navList->addItem(tr("销售业绩"));
	m_navList->addItem(tr("电桩状态"));
	m_navList->addItem(tr("充电桩管理"));
	m_navList->addItem(tr("充电站管理"));
	m_navList->addItem(tr("用户管理"));
	sideLayout->addWidget(m_navList, 1);

	m_userLabel = new QLabel(side);
	m_userLabel->setObjectName(QStringLiteral("userLabel"));
	sideLayout->addWidget(m_userLabel);

	// 权限提示:ADMIN_READONLY 无写权限
	connect(m_api, &ops::ApiClient::loginSucceeded, this, [this](const ops::AdminUser &admin) {
		m_userLabel->setText(QStringLiteral("%1\n%2").arg(
			admin.displayName,
			admin.role == QLatin1String("ADMIN_READONLY") ? tr("(只读)") : tr("(管理员)")));
	});

	layout()->addWidget(side);
}

void MainWindow::resumeAfterLogin() {
	statusBar()->showMessage(tr("已登录"));
}

QWidget *MainWindow::createPage(int index) {
	switch (index) {
	case PageSales:
		m_salesPage = new SalesPage(m_api, this);
		return m_salesPage;
	case PageChargerStatus:
		m_chargerStatusPage = new ChargerStatusPage(m_api, this);
		return m_chargerStatusPage;
	case PageChargerManage:
		m_chargerManagePage = new ChargerManagePage(m_api, this);
		return m_chargerManagePage;
	case PageStationManage:
		m_stationPage = new StationPage(m_api, this);
		return m_stationPage;
	case PageUserManage:
		m_userPage = new UserPage(m_api, this);
		return m_userPage;
	}
	return new QWidget(this);
}
