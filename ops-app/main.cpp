/** @file
 * @brief 组装管理员应用、全局深色主题和共享客户端，以登录循环管理认证失效后的窗口重建。
 */
#include "logindialog.h"
#include "mainwindow.h"
#include <QThread>
#include <evcharger/logging.h>

#include <QApplication>
#include <QEventLoop>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>

Q_LOGGING_CATEGORY(opsLifecycle, "evcharger.ops.lifecycle", QtInfoMsg)

namespace {

	// 深色主题,视觉基调参考 Qt 官方 Thermostat 示例(Widgets + QSS 实现)
	/// @brief 应用级深色 QSS；通过对象名区分导航、危险操作和指标卡片外观。
	const char *kAppStyleSheet = R"(
QMainWindow, QDialog {
    background-color: #1b1e23;
    color: #e8eaed;
}
QWidget {
    background-color: #1b1e23;
    color: #e8eaed;
    font-size: 14px;
}
#sidebar {
    background-color: #22262c;
    border-right: 1px solid #2d323a;
}
#brand {
    color: #4da3ff;
    font-size: 16px;
    font-weight: bold;
    background: transparent;
}
#navList {
    background: transparent;
    outline: none;
}
#navList::item {
    padding: 12px 16px;
    border-radius: 8px;
    margin: 2px 0px;
    color: #aab1bb;
}
#navList::item:selected {
    background-color: #2d323a;
    color: #ffffff;
}
#navList::item:hover {
    background-color: #282d34;
}
#userLabel {
    color: #8a8f98;
    background: transparent;
}
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: #262b32;
    border: 1px solid #333a43;
    border-radius: 6px;
    padding: 8px 10px;
    selection-background-color: #35507a;
}
QLineEdit:focus, QComboBox:focus {
    border-color: #4da3ff;
}
QPushButton {
    background-color: #2d323a;
    border: none;
    border-radius: 6px;
    padding: 8px 18px;
    color: #e8eaed;
}
QPushButton:hover { background-color: #363c46; }
QPushButton:pressed { background-color: #262b32; }
QPushButton:disabled { color: #6b7280; background-color: #262b32; }
QPushButton#primary { background-color: #35507a; }
QPushButton#primary:hover { background-color: #3f5d8d; }
QPushButton#danger { background-color: #6e2f34; }
QPushButton#danger:hover { background-color: #7d383e; }
QTableWidget {
    background-color: #22262c;
    alternate-background-color: #262b32;
    border: 1px solid #2d323a;
    border-radius: 8px;
    gridline-color: #2d323a;
    selection-background-color: #35507a;
}
QHeaderView::section {
    background-color: #262b32;
    color: #aab1bb;
    border: none;
    border-bottom: 1px solid #2d323a;
    padding: 8px;
}
QStatusBar {
    background-color: #22262c;
    color: #8a8f98;
}
QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollBar::handle:vertical {
    background: #333a43; border-radius: 5px; min-height: 30px;
}
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QLabel[card="true"] {
    background-color: #22262c;
    border: 1px solid #2d323a;
    border-radius: 10px;
    padding: 16px;
}
QMessageBox { background-color: #22262c; }
)";
} // namespace

/** @brief 创建应用、共享 API 和登录循环；认证失效后销毁本轮主窗口并重新登录。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 退出码；当前正常退出路径返回零。
 */
int main(int argc, char *argv[]) {
	evcharger::logging::installMessageHandler();
	QApplication a(argc, argv);
	a.setObjectName(QStringLiteral("opsApplication"));
	QThread::currentThread()->setObjectName(QStringLiteral("opsMainThread"));
	EV_LOG_INFO(opsLifecycle, &a) << "Operations application starting; Qt=" << qVersion();
	QObject::connect(&a, &QCoreApplication::aboutToQuit, &a, [&a] { EV_LOG_INFO(opsLifecycle, &a) << "Application event loop stopping"; });
	a.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	a.setStyleSheet(QString::fromUtf8(kAppStyleSheet));
	// 应用图标(窗口/任务栏);资源由 resources/resources.qrc 打包
	a.setWindowIcon(QIcon(QStringLiteral(":/logo.png")));

	ops::ApiClient api;

	// 登录成功前不进入主界面;401 掉线时回到登录页
	for (;;) {
		LoginDialog login(&api);
		if (login.exec() != QDialog::Accepted) {
			EV_LOG_INFO(opsLifecycle, &a) << "Login cancelled; application exiting";
			return 0;
		}

		MainWindow w(&api);
		w.show();
		EV_LOG_INFO(opsLifecycle, &w) << "Operations main window shown";
		// 主界面存续期间失去认证(401)则回到登录页
		bool sessionValid = true;
		QObject::connect(&api, &ops::ApiClient::authenticationChanged, &w,
						 [&sessionValid](bool authenticated) {
							 if (!authenticated)
								 sessionValid = false;
						 });
		QEventLoop loop;
		loop.setObjectName(QStringLiteral("opsSessionEventLoop"));
		QTimer timer;
		timer.setObjectName(QStringLiteral("opsSessionMonitor"));
		timer.setInterval(200);
		QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
			if (!sessionValid)
				loop.quit();
		});
		timer.start();
		QObject::connect(&w, &QObject::destroyed, &loop, &QEventLoop::quit);
		loop.exec();
		EV_LOG_INFO(opsLifecycle, &a) << "Session event loop stopped; authenticated=" << sessionValid;
		if (sessionValid)
			break; // 用户主动关闭主窗口,正常退出
	}
	return 0;
}
