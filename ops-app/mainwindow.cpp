/** @file
 * @brief 主窗口的左侧品牌导航与右侧页面栈，创建并管理四个运营页面。
 */
#include "mainwindow.h"
#include <evcharger/logging.h>

#include "pages/chargerstatuspage.h"
#include "pages/salespage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"

#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

Q_LOGGING_CATEGORY(opsMainwindowLog, "evcharger.ops.ui", QtInfoMsg)

namespace {

	// 页面索引;每页一个独立 QWidget,由 pages_*.cpp 实现
	/// @brief 导航项与页面堆栈对应的索引。
	enum PageIndex {
		PageSales = 0,
		PageChargerStatus,
		PageStationManage,
		PageUserManage,
		PageCount
	};

	/// @brief 在 24x24 设计网格上绘制导航图标，避免为管理端引入额外图片资源。
	/// @param name 图标语义名：sales、chargers、stations、users。
	/// @param color 图标前景色。
	/// @return 36x36 逻辑尺寸、2 倍采样保证高分屏清晰的透明底位图。
	QPixmap drawNavIcon(const QString &name, const QColor &color) {
		QPixmap pixmap(72, 72);
		pixmap.setDevicePixelRatio(2.0);
		pixmap.fill(Qt::transparent);

		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(3.0, 3.0); // 24x24 网格映射到 72px 位图

		if (name == QLatin1String("sales")) { // 柱状图：三根圆头立柱与基线
			painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(6.2, 16.0), QPointF(6.2, 11.4));
			painter.drawLine(QPointF(12.0, 16.0), QPointF(12.0, 6.6));
			painter.drawLine(QPointF(17.8, 16.0), QPointF(17.8, 9.4));
			painter.drawLine(QPointF(4.8, 18.8), QPointF(19.2, 18.8));
		} else if (name == QLatin1String("chargers")) { // 闪电：充电隐喻
			painter.setPen(Qt::NoPen);
			painter.setBrush(color);
			QPainterPath bolt;
			bolt.moveTo(13.4, 2.6);
			bolt.lineTo(5.4, 13.4);
			bolt.lineTo(10.8, 13.4);
			bolt.lineTo(10.2, 21.4);
			bolt.lineTo(18.6, 10.2);
			bolt.lineTo(12.9, 10.2);
			bolt.closeSubpath();
			painter.drawPath(bolt);
		} else if (name == QLatin1String("stations")) { // 定位钉：站点隐喻
			painter.setPen(Qt::NoPen);
			painter.setBrush(color);
			QPainterPath pin;
			pin.setFillRule(Qt::WindingFill);
			pin.addEllipse(QPointF(12.0, 9.6), 5.6, 5.6);
			pin.moveTo(8.2, 13.0);
			pin.lineTo(12.0, 21.2);
			pin.lineTo(15.8, 13.0);
			pin.closeSubpath();
			painter.drawPath(pin);
			painter.setCompositionMode(QPainter::CompositionMode_Clear);
			painter.drawEllipse(QPointF(12.0, 9.6), 2.4, 2.4); // 抠出内孔
			painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
		} else { // users：双人剪影，远者降透明度制造层次
			painter.setPen(Qt::NoPen);
			painter.setBrush(color);
			painter.drawEllipse(QPointF(9.4, 8.6), 3.0, 3.0);
			QPainterPath front;
			front.addRoundedRect(QRectF(3.4, 13.2, 12.0, 7.4), 3.4, 3.4);
			painter.drawPath(front);
			painter.setOpacity(0.72);
			painter.drawEllipse(QPointF(16.8, 9.8), 2.4, 2.4);
			QPainterPath back;
			back.addRoundedRect(QRectF(13.8, 14.8, 7.6, 5.8), 2.6, 2.6);
			painter.drawPath(back);
			painter.setOpacity(1.0);
		}
		return pixmap;
	}

	/// @brief 生成普通/悬停/选中三态导航图标，选中态切换为品牌绿。
	QIcon makeNavIcon(const QString &name) {
		QIcon icon;
		const QPixmap idle = drawNavIcon(name, QColor(0x9a, 0xa3, 0xb2));
		icon.addPixmap(idle, QIcon::Normal);
		icon.addPixmap(idle, QIcon::Active);
		icon.addPixmap(drawNavIcon(name, QColor(0x34, 0xd3, 0x99)), QIcon::Selected);
		return icon;
	}

} // namespace

/// @brief 建立中央水平布局、导航与四个页面，并连接导航索引切换。
MainWindow::MainWindow(ops::ApiClient *api, QWidget *parent)
	: QMainWindow(parent), m_api(api) {
	setObjectName(QStringLiteral("opsMainWindow"));
	EV_LOG_DEBUG(opsMainwindowLog, this) << "MainWindow initialized";
	setWindowTitle(tr("充电桩运营管理平台 - 管理后台"));
	resize(1180, 760);

	auto *central = new QWidget(this);
	auto *layout = new QHBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	setCentralWidget(central);

	buildSidebar(layout); // 侧边栏加入 central 的布局(QMainWindow 自身布局不接受裸 widget)

	m_stack = new QStackedWidget(central);
	layout->addWidget(m_stack, 1);
	for (int i = 0; i < PageCount; ++i)
		m_stack->addWidget(createPage(i));

	connect(m_navList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
	connect(m_navList, &QListWidget::currentRowChanged, this, [this](int index) { EV_LOG_DEBUG(opsMainwindowLog, this) << "Navigation changed; page_index=" << index; });
	m_navList->setCurrentRow(PageSales);

	statusBar()->showMessage(tr("就绪"));
}

MainWindow::~MainWindow() = default;

/// @brief 构造左侧品牌、导航和管理员信息，并加入中央布局。
void MainWindow::buildSidebar(QHBoxLayout *layout) {
	auto *side = new QWidget(this);
	side->setObjectName(QStringLiteral("sidebar"));
	side->setFixedWidth(210);
	auto *sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(12, 24, 12, 16);
	sideLayout->setSpacing(8);

	// 品牌图标:与下方文字品牌上下排列,透明底 PNG 由 resources.qrc 打包
	auto *logoLabel = new QLabel(side);
	logoLabel->setObjectName(QStringLiteral("brandLogo"));
	logoLabel->setAlignment(Qt::AlignCenter);
	logoLabel->setStyleSheet(QStringLiteral("background: transparent;"));
	const QPixmap logoPixmap(QStringLiteral(":/logo.png"));
	if (!logoPixmap.isNull())
		logoLabel->setPixmap(logoPixmap.scaled(72, 72, Qt::KeepAspectRatio,
											   Qt::SmoothTransformation));
	sideLayout->addWidget(logoLabel);

	auto *brand = new QLabel(tr("充电桩管理平台"), side);
	brand->setObjectName(QStringLiteral("brand"));
	sideLayout->addWidget(brand);
	sideLayout->addSpacing(16);

	m_navList = new QListWidget(side);
	m_navList->setObjectName(QStringLiteral("navList"));
	m_navList->setFrameShape(QFrame::NoFrame);
	m_navList->setIconSize(QSize(20, 20));
	m_navList->addItem(tr("销售业绩"));
	m_navList->addItem(tr("电桩状态"));
	m_navList->addItem(tr("充电站管理"));
	m_navList->addItem(tr("用户管理"));
	// 导航图标:按页面顺序绘制,选中态自动切换品牌绿
	const QStringList navIconNames = {QStringLiteral("sales"),
			QStringLiteral("chargers"),
			QStringLiteral("stations"),
			QStringLiteral("users")};
	for (int i = 0; i < m_navList->count() && i < navIconNames.size(); ++i)
		m_navList->item(i)->setIcon(makeNavIcon(navIconNames.at(i)));
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

	layout->addWidget(side);
}

/// @brief 更新状态栏为已登录提示。
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
	case PageStationManage:
		m_stationPage = new StationPage(m_api, this);
		return m_stationPage;
	case PageUserManage:
		m_userPage = new UserPage(m_api, this);
		return m_userPage;
	}
	return new QWidget(this);
}
