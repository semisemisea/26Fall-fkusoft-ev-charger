/** @file
 * @brief 配置管理员应用的资源和依赖，运行 Qt 主事件循环。
 */
#include "api/apiclient.h"
#include "applicationcontroller.h"
#include <evcharger/logging.h>

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include <QThread>

Q_LOGGING_CATEGORY(opsLifecycle, "evcharger.ops.lifecycle", QtInfoMsg)

int main(int argc, char *argv[]) {
	evcharger::logging::installMessageHandler();
	QApplication a(argc, argv);
	a.setObjectName(QStringLiteral("opsApplication"));
	QThread::currentThread()->setObjectName(QStringLiteral("opsMainThread"));
	EV_LOG_INFO(opsLifecycle, &a) << "Operations application starting; Qt=" << qVersion();
	QObject::connect(&a, &QCoreApplication::aboutToQuit, &a, [&a] { EV_LOG_INFO(opsLifecycle, &a) << "Application event loop stopping"; });
	a.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	QFile styleSheet(QStringLiteral(":/style.qss"));
	if (styleSheet.open(QIODevice::ReadOnly | QIODevice::Text))
		a.setStyleSheet(QString::fromUtf8(styleSheet.readAll()));
	else
		EV_LOG_WARNING(opsLifecycle, &a) << "Unable to load application stylesheet";
	// 应用图标(窗口/任务栏);资源由 resources/resources.qrc 打包
	a.setWindowIcon(QIcon(QStringLiteral(":/logo.png")));

	ops::ApiClient api(&a);
	ApplicationController controller(api, &a);
	controller.start();
	return a.exec();
}
