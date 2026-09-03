#include <QApplication>
#include "views/mainwindow.h"

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --ignore-gpu-blocklist --enable-unsafe-swiftshader");
    }

    QApplication a(argc, argv);

    a.setStyleSheet(QStringLiteral(R"(
* {
    font-family: "PingFang SC", "Microsoft YaHei", "Noto Sans CJK SC", "WenQuanYi Micro Hei", sans-serif;
}
QWidget {
    font-size: 14px;
    color: #26282b;
}
QMainWindow, QDialog {
    background: #f2f4f8;
}
QLabel {
    background: transparent;
}
QScrollArea {
    background: transparent;
    border: none;
}
QScrollArea > QWidget > QWidget {
    background: transparent;
}
QLineEdit {
    background: white;
    border: 1px solid #dcdfe6;
    border-radius: 10px;
    padding: 10px 12px;
}
QLineEdit:focus {
    border-color: #1d5cff;
}
QPushButton {
    background: white;
    border: 1px solid #dcdfe6;
    border-radius: 10px;
    padding: 10px 14px;
    font-size: 15px;
}
QPushButton:hover {
    border-color: #1d5cff;
}
QPushButton:pressed {
    background: #eef3ff;
}
QPushButton:disabled {
    color: #a8adb5;
    border-color: #e4e6ea;
    background: #f5f6f8;
}
QPushButton#primaryButton {
    background: #1d5cff;
    color: white;
    border: none;
    font-weight: bold;
}
QPushButton#primaryButton:hover {
    background: #1a4fe0;
}
QPushButton#primaryButton:pressed {
    background: #1645c4;
}
QPushButton#dangerButton {
    background: #e74c3c;
    color: white;
    border: none;
    font-weight: bold;
    font-size: 16px;
    padding: 12px;
}
QPushButton#dangerButton:hover {
    background: #d63a2a;
}
QPushButton#warnButton {
    background: #e67e22;
    color: white;
    border: none;
    font-weight: bold;
}
QPushButton#outlineDangerButton {
    background: white;
    color: #e74c3c;
    border: 1px solid #e74c3c;
    font-weight: bold;
}
QFrame#statusBar {
    background: rgba(242, 244, 248, 0.82);
    border-bottom: 1px solid rgba(0, 0, 0, 0.05);
}
QFrame#tabPill {
    background: rgba(255, 255, 255, 0.82);
    border: 1px solid rgba(255, 255, 255, 0.9);
    border-top: 1px solid rgba(255, 255, 255, 1);
    border-radius: 26px;
}
QToolButton#tabBarButton {
    background: transparent;
    border: none;
    border-radius: 0;
    color: #8a8f99;
    font-size: 11px;
    padding: 4px 0;
}
QToolButton#tabBarButton:checked {
    color: #1d5cff;
    font-weight: bold;
}
QToolButton#tabBarButton:hover {
    background: transparent;
    color: #1d5cff;
}
QPushButton#roundBackButton {
    background: white;
    color: #26282b;
    border: 1px solid #dcdfe6;
    border-radius: 18px;
    font-size: 18px;
    font-weight: bold;
    padding: 0;
}
QPushButton#roundBackButton:hover {
    border-color: #1d5cff;
    color: #1d5cff;
}
QPushButton#roundBackButton:pressed {
    background: #eef3ff;
}
QPushButton#roundStartButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2ecc71, stop:1 #27ae60);
    color: white;
    border: none;
    border-radius: 75px;
    font-size: 22px;
    font-weight: bold;
}
QPushButton#roundStartButton:hover {
    background: #2ecc71;
    border: none;
}
QPushButton#roundStartButton:pressed {
    background: #1f8b4d;
}
QPushButton#roundStartButton:disabled {
    background: #95d5b2;
}
QListWidget {
    background: white;
    border: 1px solid #e8eaee;
    border-radius: 10px;
}
)"));

    MainWindow w;
    w.show();

    return a.exec();
}
