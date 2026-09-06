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

class UserTests : public QObject {
	Q_OBJECT

private slots:
	void managesProfileAndAvatar();
	void topUpIsAtomicAndIdempotent();
	void databaseWriteTimeoutIsServiceUnavailable();
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
		QString token;

		explicit Fixture(int busyTimeoutMs = 5000)
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), busyTimeoutMs)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			Backend::Config config;
			config.maxWalletBalanceFen = 10'000;
			Backend::registerApiRoutes(router, Backend::ApiDependencies{database, config, clock});
			const Backend::HttpResponse login = json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("13800138000")}}, {}, {});
			if (login.status != 200) {
				qFatal("Login setup failed");
			}
			token = object(login).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
		}

		Backend::HttpResponse json(const QString &method, const QString &path, const QJsonObject &body, const QString &accessToken = {}, const QByteArray &idempotencyKey = {}) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.requestId = QStringLiteral("22222222-2222-4222-8222-222222222222");
			request.headers.insert(QByteArrayLiteral("content-type"), QByteArrayLiteral("application/json"));
			if (!accessToken.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8());
			}
			if (!idempotencyKey.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("idempotency-key"), idempotencyKey);
			}
			request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
			return router.dispatch(request);
		}

		Backend::HttpResponse request(const QString &method, const QString &path) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.requestId = QStringLiteral("22222222-2222-4222-8222-222222222222");
			request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			return router.dispatch(request);
		}

		void freezeUser() {
			QString error;
			if (!database->withConnection([](QSqlDatabase &connection, QString *operationError) {
					QSqlQuery query(connection);
					if (query.exec(QStringLiteral("UPDATE users SET status = 'frozen'"))) {
						return true;
					}
					*operationError = query.lastError().text();
					return false;
				},
										  &error)) {
				qFatal("Freeze setup failed: %s", qPrintable(error));
			}
		}
	};

} // namespace

void UserTests::managesProfileAndAvatar() {
	Fixture fixture;
	const Backend::HttpResponse profile = fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me"));
	QCOMPARE(profile.status, 200);
	QCOMPARE(object(profile).value(QStringLiteral("data")).toObject().value(QStringLiteral("nickname")).toString(), QStringLiteral("用户8000"));

	const Backend::HttpResponse update = fixture.json(QStringLiteral("PATCH"), QStringLiteral("/api/v1/me"), QJsonObject{{QStringLiteral("nickname"), QStringLiteral("  小明  ")}}, fixture.token);
	QCOMPARE(update.status, 200);
	QCOMPARE(object(update).value(QStringLiteral("data")).toObject().value(QStringLiteral("nickname")).toString(), QStringLiteral("小明"));

	const QByteArray boundary = QByteArrayLiteral("test-boundary");
	const QByteArray image = QByteArray::fromHex("89504e470d0a");
	Backend::HttpRequest upload;
	upload.method = QStringLiteral("POST");
	upload.path = QStringLiteral("/api/v1/me/avatar");
	upload.requestId = QStringLiteral("22222222-2222-4222-8222-222222222222");
	upload.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + fixture.token.toUtf8());
	upload.headers.insert(QByteArrayLiteral("content-type"), QByteArrayLiteral("multipart/form-data; boundary=") + boundary);
	upload.body = QByteArrayLiteral("--") + boundary + QByteArrayLiteral("\r\nContent-Disposition: form-data; name=\"file\"; filename=\"avatar.png\"\r\nContent-Type: image/png\r\n\r\n") + image + QByteArrayLiteral("\r\n--") + boundary + QByteArrayLiteral("--\r\n");
	QCOMPARE(fixture.router.dispatch(upload).status, 200);
	const Backend::HttpResponse downloaded = fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/avatar"));
	QCOMPARE(downloaded.status, 200);
	QCOMPARE(downloaded.contentType, QByteArrayLiteral("image/png"));
	QCOMPARE(downloaded.body, image);
	QCOMPARE(fixture.request(QStringLiteral("DELETE"), QStringLiteral("/api/v1/me/avatar")).status, 204);
	QCOMPARE(fixture.request(QStringLiteral("DELETE"), QStringLiteral("/api/v1/me/avatar")).status, 204);
	QCOMPARE(fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/avatar")).status, 404);

	fixture.freezeUser();
	const Backend::HttpResponse frozenUpdate = fixture.json(QStringLiteral("PATCH"), QStringLiteral("/api/v1/me"), QJsonObject{{QStringLiteral("nickname"), QStringLiteral("新昵称")}}, fixture.token);
	QCOMPARE(frozenUpdate.status, 403);
	QCOMPARE(object(frozenUpdate).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("USER_FROZEN"));
	QCOMPARE(fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me")).status, 200);
}

void UserTests::topUpIsAtomicAndIdempotent() {
	Fixture fixture;
	const QJsonObject amount{{QStringLiteral("amountFen"), 500}};
	const Backend::HttpResponse first = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), amount, fixture.token, QByteArrayLiteral("topup-1"));
	QCOMPARE(first.status, 201);
	const Backend::HttpResponse replay = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 500}, {QStringLiteral("ignored"), true}}, fixture.token, QByteArrayLiteral("topup-1"));
	QCOMPARE(replay.status, 201);
	QCOMPARE(object(first).value(QStringLiteral("data")), object(replay).value(QStringLiteral("data")));
	const Backend::HttpResponse reused = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 600}}, fixture.token, QByteArrayLiteral("topup-1"));
	QCOMPARE(reused.status, 409);
	QCOMPARE(object(reused).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("IDEMPOTENCY_KEY_REUSED"));

	const Backend::HttpResponse wallet = fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet"));
	QCOMPARE(object(wallet).value(QStringLiteral("data")).toObject().value(QStringLiteral("balanceFen")).toInteger(), qint64(500));
	const Backend::HttpResponse transactions = fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet/transactions"));
	QCOMPARE(transactions.status, 200);
	QCOMPARE(object(transactions).value(QStringLiteral("data")).toArray().size(), 1);

	const Backend::HttpResponse overLimit = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 10'000}}, fixture.token, QByteArrayLiteral("topup-2"));
	QCOMPARE(overLimit.status, 422);
	fixture.freezeUser();
	const Backend::HttpResponse frozenTopUp = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 100}}, fixture.token, QByteArrayLiteral("topup-3"));
	QCOMPARE(frozenTopUp.status, 201);
}

void UserTests::databaseWriteTimeoutIsServiceUnavailable() {
	Fixture fixture(10);
	const QString connectionName = QStringLiteral("user-test-write-lock");
	{
		QSqlDatabase lock = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
		lock.setDatabaseName(fixture.directory.filePath(QStringLiteral("test.sqlite3")));
		QVERIFY(lock.open());
		QSqlQuery hold(lock);
		QVERIFY(hold.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
		QVERIFY(hold.exec(QStringLiteral("BEGIN IMMEDIATE")));
		QVERIFY(hold.exec(QStringLiteral("UPDATE users SET nickname=nickname")));

		const Backend::HttpResponse response = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 500}}, fixture.token, QByteArrayLiteral("locked-topup"));
		QCOMPARE(response.status, 503);
		QCOMPARE(object(response).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("SERVICE_UNAVAILABLE"));
		QVERIFY(lock.rollback());
		lock.close();
	}
	QSqlDatabase::removeDatabase(connectionName);
	QCOMPARE(fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet/transactions")).status, 200);
	QCOMPARE(object(fixture.request(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet/transactions"))).value(QStringLiteral("data")).toArray().size(), 0);
}

QTEST_APPLESS_MAIN(UserTests)

#include "user_tests.moc"
