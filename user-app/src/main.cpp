#include <QApplication>
#include <QFile>
#include <QDebug>
#include "views/mainwindow.h"

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --ignore-gpu-blocklist --enable-unsafe-swiftshader");
    }

    QApplication a(argc, argv);

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
