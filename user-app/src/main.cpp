/**
 * @file main.cpp
 * @brief 创建 QApplication、加载资源样式表并启动用户主窗口。
 */
#include <QApplication>
#include <QFile>
#include <QThread>
#include <evcharger/logging.h>

#include "views/main_window.h"

Q_LOGGING_CATEGORY(userApplication, "evcharger.user.application", QtInfoMsg)

// 程序入口：加载全局样式表后显示主窗口
/**
 * @brief 配置应用运行环境、加载全局样式并进入 Qt 事件循环。
 * @param argc 命令行参数数量，由 QApplication 解析。
 * @param argv 命令行参数数组，应用运行期间保持有效。
 * @return Qt 事件循环退出码。
 */
int main(int argc, char *argv[]) {
	evcharger::logging::installMessageHandler();
	QThread::currentThread()->setObjectName(QStringLiteral("userMainThread"));
	EV_LOG_INFO(userApplication, nullptr) << "User application starting";
	// 保留 Qt 默认图形能力；腾讯 GL 底图需要 WebGL，不强制禁用 GPU。

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
