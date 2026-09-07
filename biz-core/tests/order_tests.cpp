#include "backend/api.h"
#include "backend/database.h"
#include "backend/http.h"
#include "evcharger/clock.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <future>
#include <memory>

class OrderTests : public QObject {
	Q_OBJECT

private slots:
	void completesReservedChargingFlowExactlyOnce();
	void zeroSecondDirectOrderIsValid();
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
																															 {QStringLiteral("name"), QStringLiteral("订单测试站")},
																															 {QStringLiteral("address"), QStringLiteral("测试地址")},
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
			request.requestId = QStringLiteral("66666666-6666-4666-8666-666666666666");
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

		qint64 createCharger(double powerKw = 60) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(stationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), powerKw}}, adminToken);
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}
	};

} // namespace

void OrderTests::completesReservedChargingFlowExactlyOnce() {
	Fixture fixture;
	const qint64 chargerId = fixture.createCharger();
	const Backend::HttpResponse reservation = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), QJsonObject{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("holdMinutes"), 30}}, fixture.userToken, QByteArrayLiteral("reserve-order"));
	const qint64 reservationId = object(reservation).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const QJsonObject startBody{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("reservationId"), reservationId}};
	const Backend::HttpResponse started = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders"), startBody, fixture.userToken, QByteArrayLiteral("start-order"));
	QCOMPARE(started.status, 201);
	const qint64 orderId = object(started).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders"), startBody, fixture.userToken, QByteArrayLiteral("start-order")).status, 201);

	fixture.clock->setNowUtc(fixture.clock->nowUtc().addSecs(90));
	const Backend::HttpResponse dynamic = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/orders/%1").arg(orderId), {}, fixture.userToken);
	const QJsonObject dynamicOrder = object(dynamic).value(QStringLiteral("data")).toObject();
	QCOMPARE(dynamicOrder.value(QStringLiteral("durationSeconds")).toInteger(), qint64(90));
	QCOMPARE(dynamicOrder.value(QStringLiteral("energyKwh")).toDouble(), 1.5);
	QCOMPARE(dynamicOrder.value(QStringLiteral("amountFen")).toInteger(), qint64(150));

	QCOMPARE(fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/stations/%1").arg(fixture.stationId), QJsonObject{{QStringLiteral("priceFenPerKwh"), 999}}, fixture.adminToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/chargers/%1").arg(chargerId), QJsonObject{{QStringLiteral("powerKw"), 7.5}}, fixture.adminToken).status, 200);
	const QJsonObject snapshotted = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/orders/%1").arg(orderId), {}, fixture.userToken)).value(QStringLiteral("data")).toObject();
	QCOMPARE(snapshotted.value(QStringLiteral("powerKw")).toDouble(), 60.0);
	QCOMPARE(snapshotted.value(QStringLiteral("unitPriceFenPerKwh")).toInteger(), qint64(100));

	QString error;
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (query.exec(QStringLiteral("UPDATE users SET status='frozen'"))) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	auto firstStop = std::async(std::launch::async, [&]() { return fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/stop").arg(orderId), {}, fixture.userToken, QByteArrayLiteral("stop-1")); });
	auto secondStop = std::async(std::launch::async, [&]() { return fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/stop").arg(orderId), {}, fixture.userToken, QByteArrayLiteral("stop-2")); });
	QCOMPARE(firstStop.get().status, 200);
	QCOMPARE(secondStop.get().status, 200);
	const Backend::HttpResponse insufficient = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/settle").arg(orderId), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, fixture.userToken, QByteArrayLiteral("settle-insufficient"));
	QCOMPARE(insufficient.status, 422);
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 500}}, fixture.userToken, QByteArrayLiteral("topup-order")).status, 201);
	const Backend::HttpResponse replayedInsufficient = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/settle").arg(orderId), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, fixture.userToken, QByteArrayLiteral("settle-insufficient"));
	QCOMPARE(replayedInsufficient.status, 422);
	QCOMPARE(object(replayedInsufficient).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INSUFFICIENT_BALANCE"));

	auto firstSettle = std::async(std::launch::async, [&]() { return fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/settle").arg(orderId), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, fixture.userToken, QByteArrayLiteral("settle-1")); });
	auto secondSettle = std::async(std::launch::async, [&]() { return fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/settle").arg(orderId), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, fixture.userToken, QByteArrayLiteral("settle-2")); });
	QCOMPARE(firstSettle.get().status, 200);
	QCOMPARE(secondSettle.get().status, 200);

	int chargeCount = 0;
	int debitCount = 0;
	qint64 balance = 0;
	QString reservationStatus;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT total_charge_count FROM chargers LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		chargeCount = query.value(0).toInt();
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions WHERE type='charge_debit'")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		debitCount = query.value(0).toInt();
		if (!query.exec(QStringLiteral("SELECT balance_fen FROM users LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		balance = query.value(0).toLongLong();
		if (!query.exec(QStringLiteral("SELECT status FROM reservations LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		reservationStatus = query.value(0).toString();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(chargeCount, 1);
	QCOMPARE(debitCount, 1);
	QCOMPARE(balance, qint64(350));
	QCOMPARE(reservationStatus, QStringLiteral("used"));
	const QJsonValue active = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/me/active-order"), {}, fixture.userToken)).value(QStringLiteral("data"));
	QVERIFY(active.isNull());
}

void OrderTests::zeroSecondDirectOrderIsValid() {
	Fixture fixture;
	const qint64 chargerId = fixture.createCharger();
	const Backend::HttpResponse started = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders"), QJsonObject{{QStringLiteral("chargerId"), chargerId}}, fixture.userToken, QByteArrayLiteral("zero-start"));
	const qint64 orderId = object(started).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const Backend::HttpResponse stopped = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/stop").arg(orderId), {}, fixture.userToken, QByteArrayLiteral("zero-stop"));
	QCOMPARE(stopped.status, 200);
	QCOMPARE(object(stopped).value(QStringLiteral("data")).toObject().value(QStringLiteral("amountFen")).toInteger(), qint64(0));
	const Backend::HttpResponse settled = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/%1/settle").arg(orderId), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, fixture.userToken, QByteArrayLiteral("zero-settle"));
	QCOMPARE(settled.status, 200);
	int debitCount = 0;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions WHERE type='charge_debit' AND amount_fen=0")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		debitCount = query.value(0).toInt();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(debitCount, 1);
}

QTEST_APPLESS_MAIN(OrderTests)

#include "order_tests.moc"
