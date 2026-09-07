#include "logindialog.h"
#include "mainwindow.h"

#include <QApplication>
#include <QEventLoop>
#include <QStyleFactory>
#include <QTimer>

namespace {

	// 深色主题,视觉基调参考 Qt 官方 Thermostat 示例(Widgets + QSS 实现)
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

int main(int argc, char *argv[]) {
	QApplication a(argc, argv);
	a.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	a.setStyleSheet(QString::fromUtf8(kAppStyleSheet));

	ops::ApiClient api;

	// 登录成功前不进入主界面;401 掉线时回到登录页
	for (;;) {
		LoginDialog login(&api);
		if (login.exec() != QDialog::Accepted)
			return 0;

		MainWindow w(&api);
		w.show();
		// 主界面存续期间失去认证(401)则回到登录页
		bool sessionValid = true;
		QObject::connect(&api, &ops::ApiClient::authenticationChanged, &w,
						 [&sessionValid](bool authenticated) {
							 if (!authenticated)
								 sessionValid = false;
						 });
		QEventLoop loop;
		QTimer timer;
		timer.setInterval(200);
		QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
			if (!sessionValid)
				loop.quit();
		});
		timer.start();
		QObject::connect(&w, &QObject::destroyed, &loop, &QEventLoop::quit);
		loop.exec();
		if (sessionValid)
			break; // 用户主动关闭主窗口,正常退出
	}
	return 0;
}
