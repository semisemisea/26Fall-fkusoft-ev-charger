/**
 * @file main.cpp
 * @brief 创建 QApplication、加载资源样式表并启动用户主窗口。
 */
#include <QApplication>
#include <QFile>
#include <QDebug>
#include "views/mainwindow.h"

// 程序入口：配置 WebEngine 兼容参数，加载全局样式表后显示主窗口
/**
 * @brief 配置应用运行环境、加载全局样式并进入 Qt 事件循环。
 * @param argc 命令行参数数量，由 QApplication 解析。
 * @param argv 命令行参数数组，应用运行期间保持有效。
 * @return Qt 事件循环退出码。
 */
int main(int argc, char *argv[]) {
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
