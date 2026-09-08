#include <QApplication>
#include <QFile>
#include <QThread>
#include <evcharger/logging.h>

#include "views/mainwindow.h"

Q_LOGGING_CATEGORY(userApplication, "evcharger.user.application", QtInfoMsg)

// 程序入口：配置 WebEngine 兼容参数，加载全局样式表后显示主窗口
int main(int argc, char *argv[]) {
	evcharger::logging::installMessageHandler();
	QThread::currentThread()->setObjectName(QStringLiteral("userMainThread"));
	EV_LOG_INFO(userApplication, nullptr) << "User application starting";
	// 无 GPU 环境下的 WebEngine 兼容参数（软件渲染）
	if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
		qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --ignore-gpu-blocklist --enable-unsafe-swiftshader");
	}

	QApplication a(argc, argv);
	a.setObjectName(QStringLiteral("userApplication"));

	// 加载全局样式表（设计规范见 DESIGN.md）
	QFile styleFile(QStringLiteral(":/style.qss"));
	if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
	} else {
		EV_LOG_WARNING(userApplication, &a) << "Failed to load application stylesheet";
	}

	MainWindow w;
	w.show();

	EV_LOG_INFO(userApplication, &a) << "Main window shown";
	const int exitCode = a.exec();
	EV_LOG_INFO(userApplication, &a) << "User application stopped; exit_code=" << exitCode;
	return exitCode;
}
