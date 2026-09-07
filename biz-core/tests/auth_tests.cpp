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
#include <vector>

class AuthTests : public QObject {
	Q_OBJECT

private slots:
	void userLoginCreatesAndAuthenticatesUser();
	void concurrentFirstLoginCreatesOneUser();
	void administratorCredentialsAndServiceIdentity();
	void logoutRevokesOnlyCurrentToken();
};

namespace {

	struct ApiFixture {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<EvCharger::FixedClock> clock;
		Backend::Router router;

		ApiFixture()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QStringLiteral("service-token"), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			Backend::Config config;
			Backend::registerApiRoutes(router, Backend::ApiDependencies{database, config, clock});
		}

		Backend::HttpResponse json(const QString &method, const QString &path, const QJsonObject &body, const QString &token = {}) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.requestId = QStringLiteral("11111111-1111-4111-8111-111111111111");
			request.headers.insert(QByteArrayLiteral("content-type"), QByteArrayLiteral("application/json"));
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			}
			request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
			return router.dispatch(request);
		}

		Backend::HttpResponse get(const QString &path, const QString &token) const {
			Backend::HttpRequest request;
			request.method = QStringLiteral("GET");
			request.path = path;
			request.requestId = QStringLiteral("11111111-1111-4111-8111-111111111111");
			request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			return router.dispatch(request);
		}
	};

	QJsonObject responseObject(const Backend::HttpResponse &response) {
		return QJsonDocument::fromJson(response.body).object();
	}

	QString userLogin(ApiFixture &fixture, const QString &phone = QStringLiteral("13800138000")) {
		const Backend::HttpResponse response = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), phone}});
		if (response.status != 200) {
			return {};
		}
		return responseObject(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
	}

} // namespace

void AuthTests::userLoginCreatesAndAuthenticatesUser() {
	ApiFixture fixture;
	const Backend::HttpResponse invalid = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("１３８００１３８０００")}});
	QCOMPARE(invalid.status, 400);

	const QString token = userLogin(fixture);
	QVERIFY(!token.isEmpty());
	const Backend::HttpResponse identity = fixture.get(QStringLiteral("/api/v1/auth/me"), token);
	QCOMPARE(identity.status, 200);
	const QJsonObject data = responseObject(identity).value(QStringLiteral("data")).toObject();
	QCOMPARE(data.value(QStringLiteral("principalType")).toString(), QStringLiteral("user"));
	QCOMPARE(data.value(QStringLiteral("role")).toString(), QStringLiteral("USER"));

	QByteArray storedHash;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT token_hash FROM access_tokens LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		storedHash = query.value(0).toByteArray();
		return true;
	},
											  &error),
			 qPrintable(error));
	QVERIFY(storedHash != token.toUtf8());
}

void AuthTests::concurrentFirstLoginCreatesOneUser() {
	ApiFixture fixture;
	std::vector<std::future<QString>> attempts;
	for (int index = 0; index < 8; ++index) {
		attempts.push_back(std::async(std::launch::async, [&fixture]() {
			return userLogin(fixture, QStringLiteral("13900139000"));
		}));
	}
	for (auto &attempt : attempts) {
		QVERIFY(!attempt.get().isEmpty());
	}
	int userCount = 0;
	int tokenCount = 0;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM users WHERE phone = '13900139000'")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		userCount = query.value(0).toInt();
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM access_tokens WHERE principal_type = 'user'")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		tokenCount = query.value(0).toInt();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(userCount, 1);
	QCOMPARE(tokenCount, 8);
}

void AuthTests::administratorCredentialsAndServiceIdentity() {
	ApiFixture fixture;
	const Backend::HttpResponse invalid = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{
																															   {QStringLiteral("username"), QStringLiteral("missing")},
																															   {QStringLiteral("password"), QStringLiteral("wrong")},
																														   });
	QCOMPARE(invalid.status, 401);
	QCOMPARE(responseObject(invalid).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_CREDENTIALS"));

	const Backend::HttpResponse login = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{
																															 {QStringLiteral("username"), QStringLiteral("admin")},
																															 {QStringLiteral("password"), QStringLiteral("123456")},
																														 });
	QCOMPARE(login.status, 200);
	const QString adminToken = responseObject(login).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
	QCOMPARE(responseObject(fixture.get(QStringLiteral("/api/v1/auth/me"), adminToken)).value(QStringLiteral("data")).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("ADMIN"));

	const Backend::HttpResponse service = fixture.get(QStringLiteral("/api/v1/auth/me"), QStringLiteral("service-token"));
	QCOMPARE(service.status, 200);
	const QJsonObject serviceData = responseObject(service).value(QStringLiteral("data")).toObject();
	QCOMPARE(serviceData.value(QStringLiteral("role")).toString(), QStringLiteral("SERVICE"));
	QCOMPARE(serviceData.value(QStringLiteral("serviceName")).toString(), QStringLiteral("ml"));
	const Backend::HttpResponse serviceLogout = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/logout"), {}, QStringLiteral("service-token"));
	QCOMPARE(serviceLogout.status, 403);
}

void AuthTests::logoutRevokesOnlyCurrentToken() {
	ApiFixture fixture;
	const QString firstToken = userLogin(fixture);
	const QString secondToken = userLogin(fixture);
	QVERIFY(!firstToken.isEmpty());
	QVERIFY(!secondToken.isEmpty());
	const Backend::HttpResponse logout = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/logout"), {}, firstToken);
	QCOMPARE(logout.status, 204);
	QVERIFY(logout.body.isEmpty());
	QCOMPARE(fixture.get(QStringLiteral("/api/v1/auth/me"), firstToken).status, 401);
	QCOMPARE(fixture.get(QStringLiteral("/api/v1/auth/me"), secondToken).status, 200);
	fixture.clock->setNowUtc(fixture.clock->nowUtc().addDays(7));
	QCOMPARE(fixture.get(QStringLiteral("/api/v1/auth/me"), secondToken).status, 401);
}

QTEST_APPLESS_MAIN(AuthTests)

#include "auth_tests.moc"
