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

	volatile std::sig_atomic_t stopRequested = 0;

	void requestStop(int) {
		stopRequested = 1;
	}

} // namespace

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
	auto mapClient = std::make_shared<Backend::TencentMapClient>(config->tencentMapKey, config->mapTimeoutMs, config->mapRetryCount);
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
