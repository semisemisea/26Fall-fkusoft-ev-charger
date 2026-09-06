#include <QApplication>
#include <QFile>
#include <QDebug>
#include "views/mainwindow.h"

// 程序入口：配置 WebEngine 兼容参数，加载全局样式表后显示主窗口
int main(int argc, char *argv[])
{
    // 无 GPU 环境下的 WebEngine 兼容参数（软件渲染）
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --ignore-gpu-blocklist --enable-unsafe-swiftshader");
    }

    QApplication a(argc, argv);

    // 加载全局样式表（设计规范见 DESIGN.md）
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        qWarning("failed to load :/style.qss");
    }

    MainWindow w;
    w.show();

    return a.exec();
}
