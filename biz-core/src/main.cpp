/**
 * @file main.cpp
 * @brief 后端进程入口，装配依赖并响应退出信号。
 */
#include "evcharger/logging.h"

#include "backend/api.h"
#include "backend/config.h"
#include "backend/database.h"
#include "backend/http.h"
#include "backend/map_client.h"
#include "evcharger/clock.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QTimer>

#include <csignal>
#include <memory>

Q_LOGGING_CATEGORY(backendLifecycle, "evcharger.backend.lifecycle", QtInfoMsg)

namespace {

	/// @brief 信号处理器置位、Qt 定时器轮询的退出请求标志。
	volatile std::sig_atomic_t stopRequested = 0;

	/**
	 * @brief 在信号处理器中仅设置退出标志，实际 Qt 退出由事件循环处理。
	 */
	void requestStop(int) {
		stopRequested = 1;
	}

} // namespace

/**
 * @brief 加载配置、初始化数据库和路由后启动 HTTP 服务，退出时按配置期限停止服务。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 正常退出返回事件循环退出码，配置或初始化失败时返回非零值。
 */
int main(int argc, char *argv[]) {
	evcharger::logging::installMessageHandler();
	QCoreApplication application(argc, argv);
	application.setObjectName(QStringLiteral("backend-application"));
	EV_LOG_INFO(backendLifecycle, &application) << "Backend starting" << "qtVersion=" << qVersion();
	const QString executableDirectory = QCoreApplication::applicationDirPath();
	const QString configPath = QDir(executableDirectory).filePath(QStringLiteral("config.ini"));
	QString errorMessage;
	const auto config = Backend::Config::load(configPath, executableDirectory, QProcessEnvironment::systemEnvironment(), &errorMessage);
	if (!config.has_value()) {
		EV_LOG_CRITICAL(backendLifecycle, &application) << "Configuration validation failed" << "reason=" << errorMessage;
		return 1;
	}

	auto database = std::make_shared<Backend::Database>(config->databasePath, config->databaseBusyTimeoutMs);
	if (!database->initialize(QDateTime::currentDateTimeUtc(), config->serviceToken, &errorMessage)) {
		EV_LOG_CRITICAL(backendLifecycle, &application) << "Database initialization failed" << "reason=" << errorMessage;
		return 1;
	}

	auto router = std::make_shared<Backend::Router>();
	auto clock = std::make_shared<EvCharger::SystemClock>();
	auto mapClient = std::make_shared<Backend::TencentMapClient>(config->tencentMapKey, config->mapTimeoutMs, config->mapRetryCount, config->tencentMapSecretKey);
	Backend::registerApiRoutes(*router, Backend::ApiDependencies{database, *config, clock, mapClient});

	Backend::HttpServer server(router, config->jsonBodyLimitBytes, config->avatarBodyLimitBytes);
	if (!server.start(QHostAddress(config->host), config->port, &errorMessage)) {
		EV_LOG_CRITICAL(backendLifecycle, &application) << "HTTP server failed to start:" << errorMessage;
		return 1;
	}
	QObject::connect(&application, &QCoreApplication::aboutToQuit, &server, [&server, config]() {
		server.stop(config->shutdownTimeoutMs);
		EV_LOG_INFO(backendLifecycle, &server) << "Backend stopped";
	});
	std::signal(SIGINT, requestStop);
	std::signal(SIGTERM, requestStop);
	QTimer signalTimer;
	QObject::connect(&signalTimer, &QTimer::timeout, &application, [&application]() {
		if (stopRequested != 0) {
			application.quit();
		}
	});
	signalTimer.start(100);
	EV_LOG_INFO(backendLifecycle, &server) << "Backend ready" << "host=" << config->host << "port=" << server.serverPort();
	return application.exec();
}
