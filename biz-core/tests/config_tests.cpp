/**
 * @file config_tests.cpp
 * @brief 自动化测试：配置默认值、环境变量覆盖和业务上下限校验。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

#include "backend/config.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

/** @brief 配置默认值、环境变量覆盖和业务上下限校验。的 Qt Test 测试集合。 */
class ConfigTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证默认数据库相对路径以程序目录为基准。
	 */
	void defaultsAreResolvedFromExecutableDirectory();
	/**
	 * @brief 验证环境变量覆盖 INI 配置。
	 */
	void environmentOverridesIni();
	/**
	 * @brief 验证显式非法配置触发加载失败而非静默使用默认值。
	 */
	void configuredInvalidValueFails();
	/**
	 * @brief 验证不一致的钱包充值及余额上限被拒绝。
	 */
	void inconsistentWalletLimitsFail();
};

/**
 * @brief 验证默认数据库相对路径以程序目录为基准。
 */
void ConfigTests::defaultsAreResolvedFromExecutableDirectory() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	QString error;
	const auto config = Backend::Config::load(directory.filePath(QStringLiteral("missing.ini")), directory.path(), QProcessEnvironment(), &error);
	QVERIFY2(config.has_value(), qPrintable(error));
	QCOMPARE(config->host, QStringLiteral("127.0.0.1"));
	QCOMPARE(config->port, quint16(8080));
	QVERIFY(config->tencentMapSecretKey.isEmpty());
	QCOMPARE(config->databasePath, directory.filePath(QStringLiteral("data/ev-charger.sqlite3")));
	QCOMPARE(config->maxChargerPowerW, qint64(1'000'000));
}

/**
 * @brief 验证环境变量覆盖 INI 配置。
 */
void ConfigTests::environmentOverridesIni() {
	QTemporaryDir directory;
	QFile file(directory.filePath(QStringLiteral("config.ini")));
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
	QTextStream stream(&file);
	stream << "[server]\nport=9000\n[charger]\nmaxPowerKw=60.125\n";
	file.close();

	QProcessEnvironment environment;
	environment.insert(QStringLiteral("EV_CHARGER_HTTP_PORT"), QStringLiteral("8123"));
	environment.insert(QStringLiteral("TENCENT_MAP_KEY"), QStringLiteral("test-key"));
	environment.insert(QStringLiteral("TENCENT_MAP_SECRET_KEY"), QStringLiteral("test-secret"));
	QString error;
	const auto config = Backend::Config::load(file.fileName(), directory.path(), environment, &error);
	QVERIFY2(config.has_value(), qPrintable(error));
	QCOMPARE(config->port, quint16(8123));
	QCOMPARE(config->tencentMapKey, QStringLiteral("test-key"));
	QCOMPARE(config->tencentMapSecretKey, QStringLiteral("test-secret"));
	QCOMPARE(config->maxChargerPowerW, qint64(60'125));
}

/**
 * @brief 验证显式非法配置触发加载失败而非静默使用默认值。
 */
void ConfigTests::configuredInvalidValueFails() {
	QTemporaryDir directory;
	QProcessEnvironment environment;
	environment.insert(QStringLiteral("EV_CHARGER_HTTP_PORT"), QStringLiteral("not-a-port"));
	QString error;
	const auto config = Backend::Config::load(directory.filePath(QStringLiteral("config.ini")), directory.path(), environment, &error);
	QVERIFY(!config.has_value());
	QCOMPARE(error, QStringLiteral("Invalid value for server/port"));
}

/**
 * @brief 验证不一致的钱包充值及余额上限被拒绝。
 */
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
