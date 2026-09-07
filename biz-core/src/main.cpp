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

namespace {

	volatile std::sig_atomic_t stopRequested = 0;

	void requestStop(int) {
		stopRequested = 1;
	}

} // namespace

int main(int argc, char *argv[]) {
	QCoreApplication application(argc, argv);
	const QString executableDirectory = QCoreApplication::applicationDirPath();
	const QString configPath = QDir(executableDirectory).filePath(QStringLiteral("config.ini"));
	QString errorMessage;
	const auto config = Backend::Config::load(configPath, executableDirectory, QProcessEnvironment::systemEnvironment(), &errorMessage);
	if (!config.has_value()) {
		qCritical().noquote() << "Configuration error:" << errorMessage;
		return 1;
	}

	auto database = std::make_shared<Backend::Database>(config->databasePath, config->databaseBusyTimeoutMs);
	if (!database->initialize(QDateTime::currentDateTimeUtc(), config->serviceToken, &errorMessage)) {
		qCritical().noquote() << "Database initialization failed";
		return 1;
	}

	auto router = std::make_shared<Backend::Router>();
	auto clock = std::make_shared<EvCharger::SystemClock>();
	auto mapClient = std::make_shared<Backend::TencentMapClient>(config->tencentMapKey, config->mapTimeoutMs, config->mapRetryCount);
	Backend::registerApiRoutes(*router, Backend::ApiDependencies{database, *config, clock, mapClient});

	Backend::HttpServer server(router, config->jsonBodyLimitBytes, config->avatarBodyLimitBytes);
	if (!server.start(QHostAddress(config->host), config->port, &errorMessage)) {
		qCritical().noquote() << "HTTP server failed to start:" << errorMessage;
		return 1;
	}
	QObject::connect(&application, &QCoreApplication::aboutToQuit, &server, [&server, config]() {
		server.stop(config->shutdownTimeoutMs);
		qInfo() << "Backend stopped";
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
	qInfo().noquote() << "Backend listening on" << config->host << ':' << server.serverPort();
	return application.exec();
}
