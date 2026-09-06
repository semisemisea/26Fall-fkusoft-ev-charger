#include "backend/config.h"
#include "backend/database.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

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

	Backend::Database database(config->databasePath, config->databaseBusyTimeoutMs);
	if (!database.initialize(QDateTime::currentDateTimeUtc(), config->serviceToken, &errorMessage)) {
		qCritical().noquote() << "Database initialization failed";
		return 1;
	}

	qInfo().noquote() << "Backend initialized for" << config->host << ':' << config->port;
	return 0;
}
