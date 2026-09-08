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

#include <future>
#include <memory>

class ReservationTests : public QObject {
	Q_OBJECT

private slots:
	void createsReplaysExpiresAndCancels();
	void concurrentReservationHasSingleWinner();
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
		QString firstUserToken;
		QString secondUserToken;
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
			firstUserToken = login(QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("13800138000")}});
			secondUserToken = login(QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("13900139000")}});
			const Backend::HttpResponse station = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															 {QStringLiteral("name"), QStringLiteral("预约测试站")},
																															 {QStringLiteral("latitude"), 0},
																															 {QStringLiteral("longitude"), 0},
																															 {QStringLiteral("priceFenPerKwh"), 100},
																														 },
													   adminToken);
			stationId = object(station).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		Backend::HttpResponse send(const QString &method, const QString &path, const QJsonObject &body = {}, const QString &token = {}, const QByteArray &key = {}) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.requestId = QStringLiteral("55555555-5555-4555-8555-555555555555");
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			}
			if (!key.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("idempotency-key"), key);
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

		qint64 createCharger() {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(stationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 60}}, adminToken);
			if (response.status != 201) {
				qFatal("Charger setup failed");
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		Backend::HttpResponse reserve(qint64 chargerId, const QString &token, const QByteArray &key, int minutes = 30, bool unknown = false) const {
			QJsonObject body{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("holdMinutes"), minutes}};
			if (unknown) {
				body.insert(QStringLiteral("ignored"), true);
			}
			return send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), body, token, key);
		}
	};

} // namespace

void ReservationTests::createsReplaysExpiresAndCancels() {
	Fixture fixture;
	const qint64 firstCharger = fixture.createCharger();
	const Backend::HttpResponse created = fixture.reserve(firstCharger, fixture.firstUserToken, QByteArrayLiteral("reservation-1"));
	QCOMPARE(created.status, 201);
	const qint64 reservationId = object(created).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const Backend::HttpResponse replay = fixture.reserve(firstCharger, fixture.firstUserToken, QByteArrayLiteral("reservation-1"), 30, true);
	QCOMPARE(replay.status, 201);
	QCOMPARE(object(replay).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger(), reservationId);
	QCOMPARE(fixture.reserve(firstCharger, fixture.firstUserToken, QByteArrayLiteral("reservation-1"), 20).status, 409);

	fixture.clock->setNowUtc(fixture.clock->nowUtc().addSecs(30 * 60));
	const Backend::HttpResponse detail = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations/%1").arg(reservationId), {}, fixture.firstUserToken);
	QCOMPARE(object(detail).value(QStringLiteral("data")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("expired"));
	const Backend::HttpResponse charger = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/chargers/%1").arg(firstCharger), {}, fixture.firstUserToken);
	QCOMPARE(object(charger).value(QStringLiteral("data")).toObject().value(QStringLiteral("occupancyStatus")).toString(), QStringLiteral("available"));

	const qint64 secondCharger = fixture.createCharger();
	const Backend::HttpResponse second = fixture.reserve(secondCharger, fixture.firstUserToken, QByteArrayLiteral("reservation-2"));
	const qint64 secondReservationId = object(second).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const Backend::HttpResponse cancelled = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations/%1/cancel").arg(secondReservationId), {}, fixture.firstUserToken);
	QCOMPARE(cancelled.status, 200);
	QCOMPARE(object(cancelled).value(QStringLiteral("data")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations/%1/cancel").arg(secondReservationId), {}, fixture.firstUserToken).status, 409);
	QString error;
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (query.exec(QStringLiteral("UPDATE users SET status='frozen' WHERE phone='13800138000'"))) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations"), {}, fixture.firstUserToken).status, 200);
	const qint64 frozenCharger = fixture.createCharger();
	const Backend::HttpResponse frozenCreate = fixture.reserve(frozenCharger, fixture.firstUserToken, QByteArrayLiteral("reservation-frozen"));
	QCOMPARE(frozenCreate.status, 403);
	QCOMPARE(object(frozenCreate).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("USER_FROZEN"));
}

void ReservationTests::concurrentReservationHasSingleWinner() {
	Fixture fixture;
	const qint64 chargerId = fixture.createCharger();
	auto first = std::async(std::launch::async, [&]() { return fixture.reserve(chargerId, fixture.firstUserToken, QByteArrayLiteral("concurrent-1")); });
	auto second = std::async(std::launch::async, [&]() { return fixture.reserve(chargerId, fixture.secondUserToken, QByteArrayLiteral("concurrent-2")); });
	const Backend::HttpResponse firstResponse = first.get();
	const Backend::HttpResponse secondResponse = second.get();
	QVERIFY((firstResponse.status == 201 && secondResponse.status == 409) || (firstResponse.status == 409 && secondResponse.status == 201));

	int activeCount = 0;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE charger_id=1 AND status='active'")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		activeCount = query.value(0).toInt();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(activeCount, 1);
}

QTEST_APPLESS_MAIN(ReservationTests)

#include "reservation_tests.moc"
