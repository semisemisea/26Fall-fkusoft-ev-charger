#include "backend/api.h"
#include "backend/database.h"
#include "backend/http.h"
#include "evcharger/clock.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class ChargerTests : public QObject {
	Q_OBJECT

private slots:
	void createsFiltersAndReadsChargers();
	void faultExpiresReservationAndDeletionRespectsOrders();
};

namespace {

	QJsonObject object(const Backend::HttpResponse &response) {
		return QJsonDocument::fromJson(response.body).object();
	}

	struct Fixture {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<EvCharger::FixedClock> clock;
		Backend::Router router;
		QString adminToken;
		QString userToken;
		qint64 stationId = 0;

		Fixture()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			Backend::Config config;
			Backend::registerApiRoutes(router, Backend::ApiDependencies{database, config, clock});
			adminToken = login(QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("admin")}, {QStringLiteral("password"), QStringLiteral("123456")}});
			userToken = login(QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("13800138000")}});
			const Backend::HttpResponse station = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															 {QStringLiteral("name"), QStringLiteral("测试站")},
																															 {QStringLiteral("address"), QStringLiteral("测试地址")},
																															 {QStringLiteral("latitude"), 38.889},
																															 {QStringLiteral("longitude"), 121.537},
																															 {QStringLiteral("priceFenPerKwh"), 100},
																														 },
													   adminToken);
			stationId = object(station).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		Backend::HttpResponse send(const QString &method, const QString &path, const QJsonObject &body = {}, const QString &token = {}, const QUrlQuery &query = {}) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.query = query;
			request.requestId = QStringLiteral("44444444-4444-4444-8444-444444444444");
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			}
			if (method == QStringLiteral("POST") || method == QStringLiteral("PATCH")) {
				request.headers.insert(QByteArrayLiteral("content-type"), QByteArrayLiteral("application/json"));
				request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
			}
			return router.dispatch(request);
		}

		QString login(const QString &path, const QJsonObject &body) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), path, body);
			if (response.status != 200) {
				qFatal("Login setup failed");
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
		}

		qint64 createCharger(const QString &type = QStringLiteral("fast"), double power = 120.125) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(stationId), QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("powerKw"), power}}, adminToken);
			if (response.status != 201) {
				qFatal("Charger setup failed: %s", response.body.constData());
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}
	};

} // namespace

void ChargerTests::createsFiltersAndReadsChargers() {
	Fixture fixture;
	const qint64 fastId = fixture.createCharger();
	fixture.createCharger(QStringLiteral("slow"), 7.5);
	const Backend::HttpResponse detail = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/chargers/%1").arg(fastId), {}, fixture.userToken);
	QCOMPARE(detail.status, 200);
	const QJsonObject charger = object(detail).value(QStringLiteral("data")).toObject();
	QCOMPARE(charger.value(QStringLiteral("powerKw")).toDouble(), 120.125);
	QCOMPARE(charger.value(QStringLiteral("occupancyStatus")).toString(), QStringLiteral("available"));
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/chargers/%1").arg(fastId)).status, 401);

	QUrlQuery filters;
	filters.addQueryItem(QStringLiteral("type"), QStringLiteral("slow"));
	const Backend::HttpResponse list = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/%1/chargers").arg(fixture.stationId), {}, {}, filters);
	QCOMPARE(list.status, 200);
	const QJsonArray items = object(list).value(QStringLiteral("data")).toArray();
	QCOMPARE(items.size(), 1);
	QCOMPARE(items.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("slow"));

	const Backend::HttpResponse invalidPower = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(fixture.stationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 1.0001}}, fixture.adminToken);
	QCOMPARE(invalidPower.status, 400);
}

void ChargerTests::faultExpiresReservationAndDeletionRespectsOrders() {
	Fixture fixture;
	const qint64 chargerId = fixture.createCharger();
	qint64 userId = 0;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery user(database);
		if (!user.exec(QStringLiteral("SELECT id FROM users LIMIT 1")) || !user.next()) {
			*operationError = user.lastError().text();
			return false;
		}
		userId = user.value(0).toLongLong();
		QSqlQuery reserve(database);
		reserve.prepare(QStringLiteral("INSERT INTO reservations(user_id,station_id,charger_id,status,created_at,expires_at,updated_at) VALUES (?,?,?,'active',?,?,?)"));
		reserve.addBindValue(userId);
		reserve.addBindValue(fixture.stationId);
		reserve.addBindValue(chargerId);
		reserve.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		reserve.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc().addSecs(600)));
		reserve.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		QSqlQuery occupy(database);
		occupy.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='reserved' WHERE id=?"));
		occupy.addBindValue(chargerId);
		if (reserve.exec() && occupy.exec()) {
			return true;
		}
		*operationError = reserve.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));

	const Backend::HttpResponse fault = fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/chargers/%1").arg(chargerId), QJsonObject{{QStringLiteral("operationalStatus"), QStringLiteral("fault")}}, fixture.adminToken);
	QCOMPARE(fault.status, 200);
	const QJsonObject faulted = object(fault).value(QStringLiteral("data")).toObject();
	QCOMPARE(faulted.value(QStringLiteral("occupancyStatus")).toString(), QStringLiteral("available"));
	QCOMPARE(faulted.value(QStringLiteral("operationalStatus")).toString(), QStringLiteral("fault"));
	QString reservationStatus;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT status FROM reservations LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		reservationStatus = query.value(0).toString();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(reservationStatus, QStringLiteral("expired"));
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/chargers/%1/restart").arg(chargerId), {}, fixture.adminToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/chargers/%1/restart").arg(chargerId), {}, fixture.adminToken).status, 409);

	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery order(database);
		order.prepare(QStringLiteral("INSERT INTO orders(user_id,station_id,charger_id,status,power_w,unit_price_fen_per_kwh,started_at,created_at,updated_at) VALUES (?,?,?,'charging',120125,100,?,?,?)"));
		order.addBindValue(userId);
		order.addBindValue(fixture.stationId);
		order.addBindValue(chargerId);
		order.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		order.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		order.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		QSqlQuery occupy(database);
		occupy.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='charging' WHERE id=?"));
		occupy.addBindValue(chargerId);
		if (order.exec() && occupy.exec()) {
			return true;
		}
		*operationError = order.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/chargers/%1").arg(chargerId), {}, fixture.adminToken).status, 409);
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery order(database);
		QSqlQuery charger(database);
		if (order.exec(QStringLiteral("UPDATE orders SET status='awaiting_payment',duration_seconds=0,energy_ws=0,amount_fen=0")) && charger.exec(QStringLiteral("UPDATE chargers SET occupancy_status='available'"))) {
			return true;
		}
		*operationError = order.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/chargers/%1").arg(chargerId), {}, fixture.adminToken).status, 204);
	QCOMPARE(fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/chargers/%1").arg(chargerId), {}, fixture.adminToken).status, 204);
	QUrlQuery includeDeleted;
	includeDeleted.addQueryItem(QStringLiteral("includeDeleted"), QStringLiteral("true"));
	const Backend::HttpResponse deletedList = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/chargers"), {}, fixture.adminToken, includeDeleted);
	QCOMPARE(object(deletedList).value(QStringLiteral("data")).toArray().size(), 1);
}

QTEST_APPLESS_MAIN(ChargerTests)

#include "charger_tests.moc"
