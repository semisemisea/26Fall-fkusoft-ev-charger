#include "backend/database.h"

#include "backend/security.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class DatabaseTests : public QObject {
	Q_OBJECT

private slots:
	void initializesSchemaAndHashedDefaultAdmin();
	void configuresEveryConnection();
	void startupRecoversReservationsAndChargingOccupancy();
};

namespace {

	bool execute(QSqlDatabase &database, const QString &sql, QString *error) {
		QSqlQuery query(database);
		if (query.exec(sql)) {
			return true;
		}
		*error = query.lastError().text();
		return false;
	}

} // namespace

void DatabaseTests::initializesSchemaAndHashedDefaultAdmin() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	Backend::Database database(directory.filePath(QStringLiteral("nested/test.sqlite3")), 3210);
	QString error;
	const QDateTime now(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	QVERIFY2(database.initialize(now, QStringLiteral("service-secret"), &error), qPrintable(error));
	QVERIFY2(database.initialize(now.addSecs(1), QStringLiteral("service-secret"), &error), qPrintable(error));

	QByteArray salt;
	QByteArray hash;
	int adminCount = 0;
	int schemaVersion = 0;
	QByteArray serviceHash;
	QVERIFY2(database.withConnection([&](QSqlDatabase &connection, QString *operationError) {
		QSqlQuery admin(connection);
		if (!admin.exec(QStringLiteral("SELECT password_salt, password_hash FROM admins WHERE username = 'admin'"))) {
			*operationError = admin.lastError().text();
			return false;
		}
		while (admin.next()) {
			++adminCount;
			salt = admin.value(0).toByteArray();
			hash = admin.value(1).toByteArray();
		}
		QSqlQuery version(connection);
		if (!version.exec(QStringLiteral("SELECT version FROM schema_version")) || !version.next()) {
			*operationError = version.lastError().text();
			return false;
		}
		schemaVersion = version.value(0).toInt();
		QSqlQuery service(connection);
		if (!service.exec(QStringLiteral("SELECT token_hash FROM service_credentials WHERE service_name = 'ml'")) || !service.next()) {
			*operationError = service.lastError().text();
			return false;
		}
		serviceHash = service.value(0).toByteArray();
		return true;
	},
									 &error),
			 qPrintable(error));

	QCOMPARE(adminCount, 1);
	QCOMPARE(schemaVersion, 1);
	QVERIFY(hash != QByteArrayLiteral("123456"));
	QVERIFY(Backend::Security::verifyPassword(QStringLiteral("123456"), salt, hash));
	QVERIFY(!Backend::Security::verifyPassword(QStringLiteral("wrong"), salt, hash));
	QCOMPARE(serviceHash, Backend::Security::tokenHash(QStringLiteral("service-secret")));
}

void DatabaseTests::configuresEveryConnection() {
	QTemporaryDir directory;
	Backend::Database database(directory.filePath(QStringLiteral("test.sqlite3")), 3210);
	QString error;
	QVERIFY2(database.initialize(QDateTime::currentDateTimeUtc(), QString(), &error), qPrintable(error));

	int foreignKeys = 0;
	int busyTimeout = 0;
	QString journalMode;
	QVERIFY2(database.withConnection([&](QSqlDatabase &connection, QString *operationError) {
		QSqlQuery query(connection);
		if (!query.exec(QStringLiteral("PRAGMA foreign_keys")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		foreignKeys = query.value(0).toInt();
		if (!query.exec(QStringLiteral("PRAGMA busy_timeout")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		busyTimeout = query.value(0).toInt();
		if (!query.exec(QStringLiteral("PRAGMA journal_mode")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		journalMode = query.value(0).toString();
		return true;
	},
									 &error),
			 qPrintable(error));
	QCOMPARE(foreignKeys, 1);
	QCOMPARE(busyTimeout, 3210);
	QCOMPARE(journalMode, QStringLiteral("wal"));
}

void DatabaseTests::startupRecoversReservationsAndChargingOccupancy() {
	QTemporaryDir directory;
	Backend::Database database(directory.filePath(QStringLiteral("test.sqlite3")), 5000);
	QString error;
	const QDateTime now(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	QVERIFY2(database.initialize(now.addDays(-1), QString(), &error), qPrintable(error));
	const QString past = Backend::toDatabaseTimestamp(now.addSecs(-1));
	const QString future = Backend::toDatabaseTimestamp(now.addSecs(60));
	const QString timestamp = Backend::toDatabaseTimestamp(now.addSecs(-120));
	QVERIFY2(database.withConnection([&](QSqlDatabase &connection, QString *operationError) {
		return execute(connection, QStringLiteral("INSERT INTO users(phone,nickname,status,created_at,updated_at) VALUES ('13800138001','u1','active','%1','%1'),('13800138002','u2','active','%1','%1'),('13800138003','u3','active','%1','%1')").arg(timestamp), operationError) && execute(connection, QStringLiteral("INSERT INTO stations(name,address,latitude,longitude,price_fen_per_kwh,status,created_at,updated_at) VALUES ('s','a',1,1,100,'active','%1','%1')").arg(timestamp), operationError) && execute(connection, QStringLiteral("INSERT INTO chargers(station_id,type,power_w,occupancy_status,operational_status,created_at,updated_at) VALUES (1,'fast',60000,'reserved','online','%1','%1'),(1,'fast',60000,'reserved','online','%1','%1'),(1,'fast',60000,'available','fault','%1','%1')").arg(timestamp), operationError) && execute(connection, QStringLiteral("INSERT INTO reservations(user_id,station_id,charger_id,status,created_at,expires_at,updated_at) VALUES (1,1,1,'active','%1','%2','%1'),(2,1,2,'active','%1','%3','%1')").arg(timestamp, past, future), operationError) && execute(connection, QStringLiteral("INSERT INTO orders(user_id,station_id,charger_id,status,power_w,unit_price_fen_per_kwh,started_at,created_at,updated_at) VALUES (3,1,3,'charging',60000,100,'%1','%1','%1')").arg(timestamp), operationError);
	},
									 &error),
			 qPrintable(error));

	QVERIFY2(database.initialize(now, QString(), &error), qPrintable(error));
	QStringList reservationStatuses;
	QStringList chargerStatuses;
	QString chargerOperationalStatus;
	QVERIFY2(database.withConnection([&](QSqlDatabase &connection, QString *operationError) {
		QSqlQuery reservations(connection);
		if (!reservations.exec(QStringLiteral("SELECT status FROM reservations ORDER BY id"))) {
			*operationError = reservations.lastError().text();
			return false;
		}
		while (reservations.next()) {
			reservationStatuses.append(reservations.value(0).toString());
		}
		QSqlQuery chargers(connection);
		if (!chargers.exec(QStringLiteral("SELECT occupancy_status, operational_status FROM chargers ORDER BY id"))) {
			*operationError = chargers.lastError().text();
			return false;
		}
		while (chargers.next()) {
			chargerStatuses.append(chargers.value(0).toString());
			chargerOperationalStatus = chargers.value(1).toString();
		}
		return true;
	},
									 &error),
			 qPrintable(error));
	QCOMPARE(reservationStatuses, QStringList({QStringLiteral("expired"), QStringLiteral("cancelled")}));
	QCOMPARE(chargerStatuses, QStringList({QStringLiteral("available"), QStringLiteral("available"), QStringLiteral("charging")}));
	QCOMPARE(chargerOperationalStatus, QStringLiteral("fault"));
}

QTEST_APPLESS_MAIN(DatabaseTests)

#include "database_tests.moc"
