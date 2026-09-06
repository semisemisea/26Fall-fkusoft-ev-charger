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
#include <QUrl>
#include <QUrlQuery>

#include <memory>

class AdminTests : public QObject {
	Q_OBJECT

private slots:
	void reportsRevenueSeriesAndChargerFacts();
	void rejectsInvalidDashboardRequests();
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
		qint64 userId = 0;
		qint64 stationId = 0;
		qint64 otherStationId = 0;

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
			if (!database->withConnection([&](QSqlDatabase &connection, QString *operationError) {
					QSqlQuery query(connection);
					if (!query.exec(QStringLiteral("SELECT id FROM users LIMIT 1")) || !query.next()) {
						*operationError = query.lastError().text();
						return false;
					}
					userId = query.value(0).toLongLong();
					return true;
				},
										  &error)) {
				qFatal("User setup failed: %s", qPrintable(error));
			}
			stationId = createStation(QStringLiteral("统计站"));
			otherStationId = createStation(QStringLiteral("其他站"));
		}

		Backend::HttpResponse send(const QString &method, const QString &target, const QJsonObject &body = {}, const QString &token = {}) const {
			const QUrl url(target);
			Backend::HttpRequest request;
			request.method = method;
			request.path = url.path();
			request.query = QUrlQuery(url);
			request.requestId = QStringLiteral("77777777-7777-4777-8777-777777777777");
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

		qint64 createStation(const QString &name) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															  {QStringLiteral("name"), name},
																															  {QStringLiteral("address"), QStringLiteral("测试地址")},
																															  {QStringLiteral("latitude"), 38.9},
																															  {QStringLiteral("longitude"), 121.6},
																															  {QStringLiteral("priceFenPerKwh"), 100},
																														  },
														adminToken);
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		qint64 createCharger(qint64 ownerStationId) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(ownerStationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 60}}, adminToken);
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		void insertSettledOrder(qint64 ownerStationId, qint64 chargerId, const QString &settledAt, qint64 amountFen) {
			QString error;
			const bool success = database->withConnection([&](QSqlDatabase &connection, QString *operationError) {
				QSqlQuery query(connection);
				query.prepare(QStringLiteral("INSERT INTO orders(user_id,station_id,charger_id,status,power_w,unit_price_fen_per_kwh,started_at,stopped_at,settled_at,duration_seconds,energy_ws,amount_fen,created_at,updated_at) VALUES (?,?,?,'settled',60000,100,?,?,?,60,3600000,?,?,?)"));
				query.addBindValue(userId);
				query.addBindValue(ownerStationId);
				query.addBindValue(chargerId);
				query.addBindValue(settledAt);
				query.addBindValue(settledAt);
				query.addBindValue(settledAt);
				query.addBindValue(amountFen);
				query.addBindValue(settledAt);
				query.addBindValue(settledAt);
				if (query.exec()) {
					return true;
				}
				*operationError = query.lastError().text();
				return false;
			},
														  &error);
			if (!success) {
				qFatal("Order setup failed: %s", qPrintable(error));
			}
		}
	};

} // namespace

void AdminTests::reportsRevenueSeriesAndChargerFacts() {
	Fixture fixture;
	const qint64 charger = fixture.createCharger(fixture.stationId);
	const qint64 offline = fixture.createCharger(fixture.stationId);
	const qint64 deleted = fixture.createCharger(fixture.stationId);
	const qint64 otherCharger = fixture.createCharger(fixture.otherStationId);
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		query.prepare(QStringLiteral("UPDATE chargers SET operational_status='offline' WHERE id=?"));
		query.addBindValue(offline);
		if (!query.exec()) {
			*operationError = query.lastError().text();
			return false;
		}
		query.prepare(QStringLiteral("UPDATE chargers SET deleted_at=? WHERE id=?"));
		query.addBindValue(QStringLiteral("2026-09-01T09:00:00.000Z"));
		query.addBindValue(deleted);
		if (!query.exec()) {
			*operationError = query.lastError().text();
			return false;
		}
		return true;
	},
											  &error),
			 qPrintable(error));

	fixture.insertSettledOrder(fixture.stationId, charger, QStringLiteral("2026-09-01T15:30:00.000Z"), 100);
	fixture.insertSettledOrder(fixture.stationId, charger, QStringLiteral("2026-09-02T10:00:00.000Z"), 250);
	fixture.insertSettledOrder(fixture.otherStationId, otherCharger, QStringLiteral("2026-09-02T12:00:00.000Z"), 999);

	const QJsonObject cumulative = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), {}, fixture.adminToken)).value(QStringLiteral("data")).toObject();
	QVERIFY(cumulative.value(QStringLiteral("from")).isNull());
	QCOMPARE(cumulative.value(QStringLiteral("revenueFen")).toInteger(), qint64(1349));
	QCOMPARE(cumulative.value(QStringLiteral("orderCount")).toInteger(), qint64(3));

	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		query.prepare(QStringLiteral("UPDATE stations SET deleted_at=? WHERE id=?"));
		query.addBindValue(QStringLiteral("2026-09-02T13:00:00.000Z"));
		query.addBindValue(fixture.stationId);
		if (query.exec()) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	const QString range = QStringLiteral("?from=2026-09-01T15%3A00%3A00Z&to=2026-09-02T16%3A00%3A00Z&stationId=%1").arg(fixture.stationId);
	const Backend::HttpResponse filteredResponse = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue") + range, {}, fixture.adminToken);
	QCOMPARE(filteredResponse.status, 200);
	const QJsonObject filtered = object(filteredResponse).value(QStringLiteral("data")).toObject();
	QCOMPARE(filtered.value(QStringLiteral("revenueFen")).toInteger(), qint64(350));
	QCOMPARE(filtered.value(QStringLiteral("orderCount")).toInteger(), qint64(2));

	const Backend::HttpResponse seriesResponse = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue-series") + range + QStringLiteral("&utcOffset=%2B08%3A00"), {}, fixture.adminToken);
	QCOMPARE(seriesResponse.status, 200);
	const QJsonArray points = object(seriesResponse).value(QStringLiteral("data")).toObject().value(QStringLiteral("points")).toArray();
	QCOMPARE(points.size(), 2);
	QCOMPARE(points.at(0).toObject().value(QStringLiteral("localDate")).toString(), QStringLiteral("2026-09-01"));
	QCOMPARE(points.at(0).toObject().value(QStringLiteral("revenueFen")).toInteger(), qint64(100));
	QCOMPARE(points.at(0).toObject().value(QStringLiteral("bucketStart")).toString(), QStringLiteral("2026-08-31T16:00:00.000Z"));
	QCOMPARE(points.at(1).toObject().value(QStringLiteral("revenueFen")).toInteger(), qint64(250));

	const QJsonObject facts = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/charger-status?stationId=%1").arg(fixture.stationId), {}, fixture.adminToken)).value(QStringLiteral("data")).toObject();
	QCOMPARE(facts.value(QStringLiteral("total")).toInteger(), qint64(2));
	QCOMPARE(facts.value(QStringLiteral("occupancy")).toObject().value(QStringLiteral("available")).toInteger(), qint64(2));
	QCOMPARE(facts.value(QStringLiteral("operational")).toObject().value(QStringLiteral("online")).toInteger(), qint64(1));
	QCOMPARE(facts.value(QStringLiteral("operational")).toObject().value(QStringLiteral("offline")).toInteger(), qint64(1));
}

void AdminTests::rejectsInvalidDashboardRequests() {
	Fixture fixture;
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), {}, fixture.userToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue?from=2026-09-01T00%3A00%3A00Z"), {}, fixture.adminToken).status, 400);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue-series?from=2026-09-01T00%3A00%3A00Z&to=2026-09-02T00%3A00%3A00Z&utcOffset=%2B14%3A01"), {}, fixture.adminToken).status, 400);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/charger-status?stationId=0"), {}, fixture.adminToken).status, 400);
}

QTEST_APPLESS_MAIN(AdminTests)

#include "admin_tests.moc"
