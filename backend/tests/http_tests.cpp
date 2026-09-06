#include "backend/api.h"
#include "backend/database.h"
#include "backend/http.h"
#include "evcharger/clock.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

class HttpTests : public QObject {
	Q_OBJECT

private slots:
	void servesHealthWithRequestId();
	void distinguishesMissingPathAndMethod();
	void rejectsInvalidRequestId();
	void completesUserAndAdminWorkflow();
	void stopHonorsDeadlineAndRejectsConnections();
};

namespace {

	struct ReplyData {
		int status = 0;
		QByteArray requestId;
		QJsonObject json;
	};

	ReplyData get(const QUrl &url, const QByteArray &requestId = {}) {
		QNetworkAccessManager manager;
		QNetworkRequest request(url);
		if (!requestId.isEmpty()) {
			request.setRawHeader(QByteArrayLiteral("X-Request-Id"), requestId);
		}
		QNetworkReply *reply = manager.get(request);
		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QTimer::singleShot(5000, &loop, &QEventLoop::quit);
		loop.exec();
		ReplyData result;
		result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		result.requestId = reply->rawHeader(QByteArrayLiteral("X-Request-Id"));
		result.json = QJsonDocument::fromJson(reply->readAll()).object();
		reply->deleteLater();
		return result;
	}

	ReplyData send(const QUrl &url, const QByteArray &method, const QJsonObject &body = {}, const QString &token = {}, const QByteArray &idempotencyKey = {}) {
		QNetworkAccessManager manager;
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
		if (!token.isEmpty()) {
			request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
		}
		if (!idempotencyKey.isEmpty()) {
			request.setRawHeader(QByteArrayLiteral("Idempotency-Key"), idempotencyKey);
		}
		const QByteArray payload = method == QByteArrayLiteral("GET") ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
		QNetworkReply *reply = manager.sendCustomRequest(request, method, payload);
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
		timer.start(5000);
		loop.exec();
		ReplyData result;
		result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		result.requestId = reply->rawHeader(QByteArrayLiteral("X-Request-Id"));
		result.json = QJsonDocument::fromJson(reply->readAll()).object();
		reply->deleteLater();
		return result;
	}

	struct RunningServer {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<Backend::Router> router;
		std::unique_ptr<Backend::HttpServer> server;

		RunningServer()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), router(std::make_shared<Backend::Router>()) {
			QString error;
			if (!database->initialize(QDateTime::currentDateTimeUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			router->add(QStringLiteral("GET"), QStringLiteral("/health"), [database = database](const Backend::HttpRequest &request) {
				QString error;
				const bool healthy = database->withConnection([](QSqlDatabase &connection, QString *operationError) {
					QSqlQuery query(connection);
					if (query.exec(QStringLiteral("SELECT 1")) && query.next()) {
						return true;
					}
					*operationError = query.lastError().text();
					return false;
				},
															  &error);
				return healthy
						   ? Backend::jsonData(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}}, request.requestId)
						   : Backend::jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("服务暂不可用"), {}, request.requestId, 503);
			});
			server = std::make_unique<Backend::HttpServer>(router, 1024, 2048);
			if (!server->start(QHostAddress::LocalHost, 0, &error)) {
				qFatal("Server setup failed: %s", qPrintable(error));
			}
		}

		~RunningServer() {
			server->stop(1000);
		}

		QUrl url(const QString &path) const {
			return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server->serverPort()).arg(path));
		}
	};

	struct ApiServer {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<EvCharger::FixedClock> clock;
		std::shared_ptr<Backend::Router> router;
		std::unique_ptr<Backend::HttpServer> server;

		ApiServer()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("api.sqlite3")), 5000)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))), router(std::make_shared<Backend::Router>()) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			Backend::Config config;
			Backend::registerApiRoutes(*router, Backend::ApiDependencies{database, config, clock});
			server = std::make_unique<Backend::HttpServer>(router, config.jsonBodyLimitBytes, config.avatarBodyLimitBytes);
			if (!server->start(QHostAddress::LocalHost, 0, &error)) {
				qFatal("Server setup failed: %s", qPrintable(error));
			}
		}

		~ApiServer() {
			server->stop(1000);
		}

		QUrl url(const QString &path) const {
			return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server->serverPort()).arg(path));
		}
	};

} // namespace

void HttpTests::servesHealthWithRequestId() {
	RunningServer running;
	const QByteArray requestId = QByteArrayLiteral("0c7d4f5e-7f6e-4d13-9a1c-c4b4b6725a80");
	const ReplyData reply = get(running.url(QStringLiteral("/health")), requestId);
	QVERIFY2(reply.status == 200, QJsonDocument(reply.json).toJson(QJsonDocument::Compact).constData());
	QCOMPARE(reply.requestId, requestId);
	QCOMPARE(reply.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
	QCOMPARE(reply.json.value(QStringLiteral("meta")).toObject().value(QStringLiteral("requestId")).toString(), QString::fromLatin1(requestId));
}

void HttpTests::distinguishesMissingPathAndMethod() {
	RunningServer running;
	const ReplyData missing = get(running.url(QStringLiteral("/missing")));
	QCOMPARE(missing.status, 404);
	QCOMPARE(missing.json.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

	QNetworkAccessManager manager;
	QNetworkRequest request(running.url(QStringLiteral("/health")));
	QNetworkReply *networkReply = manager.post(request, QByteArray());
	QEventLoop loop;
	QObject::connect(networkReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	QCOMPARE(networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 405);
	networkReply->deleteLater();
}

void HttpTests::rejectsInvalidRequestId() {
	RunningServer running;
	const ReplyData reply = get(running.url(QStringLiteral("/health")), QByteArrayLiteral("not-a-uuid"));
	QCOMPARE(reply.status, 400);
	QCOMPARE(reply.json.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("VALIDATION_ERROR"));
	QVERIFY(!reply.requestId.isEmpty());
}

void HttpTests::completesUserAndAdminWorkflow() {
	ApiServer running;
	const ReplyData adminLogin = send(running.url(QStringLiteral("/api/v1/auth/admin/login")), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("username"), QStringLiteral("admin")}, {QStringLiteral("password"), QStringLiteral("123456")}});
	QCOMPARE(adminLogin.status, 200);
	const QString adminToken = adminLogin.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
	const ReplyData userLogin = send(running.url(QStringLiteral("/api/v1/auth/user/login")), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("13800138000")}});
	QCOMPARE(userLogin.status, 200);
	const QString userToken = userLogin.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
	const qint64 userId = userLogin.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("user")).toObject().value(QStringLiteral("id")).toInteger();

	const ReplyData station = send(running.url(QStringLiteral("/api/v1/admin/stations")), QByteArrayLiteral("POST"), QJsonObject{
																														 {QStringLiteral("name"), QStringLiteral("HTTP 流程站")},
																														 {QStringLiteral("address"), QStringLiteral("测试地址")},
																														 {QStringLiteral("latitude"), 38.889},
																														 {QStringLiteral("longitude"), 121.537},
																														 {QStringLiteral("priceFenPerKwh"), 100},
																													 },
								   adminToken);
	QCOMPARE(station.status, 201);
	const qint64 stationId = station.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const ReplyData charger = send(running.url(QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(stationId)), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 60}}, adminToken);
	QCOMPARE(charger.status, 201);
	const qint64 chargerId = charger.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();

	const ReplyData reservation = send(running.url(QStringLiteral("/api/v1/reservations")), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("holdMinutes"), 30}}, userToken, QByteArrayLiteral("http-reservation"));
	QCOMPARE(reservation.status, 201);
	const qint64 reservationId = reservation.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
	const ReplyData order = send(running.url(QStringLiteral("/api/v1/orders")), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("reservationId"), reservationId}}, userToken, QByteArrayLiteral("http-order"));
	QCOMPARE(order.status, 201);
	const qint64 orderId = order.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();

	running.clock->setNowUtc(running.clock->nowUtc().addSecs(60));
	QCOMPARE(send(running.url(QStringLiteral("/api/v1/orders/%1/stop").arg(orderId)), QByteArrayLiteral("POST"), {}, userToken, QByteArrayLiteral("http-stop")).status, 200);
	QCOMPARE(send(running.url(QStringLiteral("/api/v1/me/wallet/topups")), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("amountFen"), 500}}, userToken, QByteArrayLiteral("http-topup")).status, 201);
	const ReplyData settled = send(running.url(QStringLiteral("/api/v1/orders/%1/settle").arg(orderId)), QByteArrayLiteral("POST"), QJsonObject{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}}, userToken, QByteArrayLiteral("http-settle"));
	QCOMPARE(settled.status, 200);
	QCOMPARE(settled.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("order")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("settled"));

	const ReplyData revenue = send(running.url(QStringLiteral("/api/v1/admin/dashboard/revenue")), QByteArrayLiteral("GET"), {}, adminToken);
	QCOMPARE(revenue.status, 200);
	QCOMPARE(revenue.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("orderCount")).toInteger(), qint64(1));
	const ReplyData frozen = send(running.url(QStringLiteral("/api/v1/admin/users/%1").arg(userId)), QByteArrayLiteral("PATCH"), QJsonObject{{QStringLiteral("status"), QStringLiteral("frozen")}}, adminToken);
	QCOMPARE(frozen.status, 200);
	QCOMPARE(send(running.url(QStringLiteral("/api/v1/admin/users/%1").arg(userId)), QByteArrayLiteral("GET"), {}, adminToken).status, 200);
}

void HttpTests::stopHonorsDeadlineAndRejectsConnections() {
	auto router = std::make_shared<Backend::Router>();
	std::atomic_bool started = false;
	std::atomic_bool release = false;
	router->add(QStringLiteral("GET"), QStringLiteral("/slow"), [&](const Backend::HttpRequest &request) {
		started = true;
		while (!release.load()) {
			QThread::msleep(1);
		}
		return Backend::jsonData(QJsonObject{}, request.requestId);
	});
	Backend::HttpServer server(router, 1024, 1024);
	QString error;
	QVERIFY2(server.start(QHostAddress::LocalHost, 0, &error), qPrintable(error));
	const quint16 port = server.serverPort();
	QNetworkAccessManager manager;
	QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/slow").arg(port))));
	QTRY_VERIFY_WITH_TIMEOUT(started.load(), 1000);

	QElapsedTimer elapsed;
	elapsed.start();
	server.stop(10);
	QVERIFY(elapsed.elapsed() < 250);
	QTcpSocket probe;
	probe.connectToHost(QHostAddress::LocalHost, port);
	QVERIFY(!probe.waitForConnected(100));
	release = true;
	QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 1000);
	reply->deleteLater();
}

QTEST_MAIN(HttpTests)

#include "http_tests.moc"
