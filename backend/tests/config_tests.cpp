#include "backend/config.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

class ConfigTests : public QObject {
	Q_OBJECT

private slots:
	void defaultsAreResolvedFromExecutableDirectory();
	void environmentOverridesIni();
	void configuredInvalidValueFails();
	void inconsistentWalletLimitsFail();
};

void ConfigTests::defaultsAreResolvedFromExecutableDirectory() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	QString error;
	const auto config = Backend::Config::load(directory.filePath(QStringLiteral("missing.ini")), directory.path(), QProcessEnvironment(), &error);
	QVERIFY2(config.has_value(), qPrintable(error));
	QCOMPARE(config->host, QStringLiteral("127.0.0.1"));
	QCOMPARE(config->port, quint16(8080));
	QCOMPARE(config->databasePath, directory.filePath(QStringLiteral("data/ev-charger.sqlite3")));
	QCOMPARE(config->maxChargerPowerW, qint64(1'000'000));
}

void ConfigTests::environmentOverridesIni() {
	QTemporaryDir directory;
	QFile file(directory.filePath(QStringLiteral("config.ini")));
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
	QTextStream stream(&file);
	stream << "[server]\nport=9000\n[charger]\nmaxPowerKw=60.125\n";
	file.close();

	QProcessEnvironment environment;
	environment.insert(QStringLiteral("EV_CHARGER_HTTP_PORT"), QStringLiteral("8123"));
	QString error;
	const auto config = Backend::Config::load(file.fileName(), directory.path(), environment, &error);
	QVERIFY2(config.has_value(), qPrintable(error));
	QCOMPARE(config->port, quint16(8123));
	QCOMPARE(config->maxChargerPowerW, qint64(60'125));
}

void ConfigTests::configuredInvalidValueFails() {
	QTemporaryDir directory;
	QProcessEnvironment environment;
	environment.insert(QStringLiteral("EV_CHARGER_HTTP_PORT"), QStringLiteral("not-a-port"));
	QString error;
	const auto config = Backend::Config::load(directory.filePath(QStringLiteral("config.ini")), directory.path(), environment, &error);
	QVERIFY(!config.has_value());
	QCOMPARE(error, QStringLiteral("Invalid value for server/port"));
}

void ConfigTests::inconsistentWalletLimitsFail() {
	QTemporaryDir directory;
	QProcessEnvironment environment;
	environment.insert(QStringLiteral("EV_CHARGER_TOP_UP_MIN_FEN"), QStringLiteral("1000"));
	environment.insert(QStringLiteral("EV_CHARGER_TOP_UP_MAX_FEN"), QStringLiteral("999"));
	QString error;
	const auto config = Backend::Config::load(directory.filePath(QStringLiteral("config.ini")), directory.path(), environment, &error);
	QVERIFY(!config.has_value());
	QCOMPARE(error, QStringLiteral("Wallet configuration limits are inconsistent"));
}

QTEST_APPLESS_MAIN(ConfigTests)

#include "config_tests.moc"
