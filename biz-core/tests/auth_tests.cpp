/**
 * @file auth_tests.cpp
 * @brief 自动化测试：登录、身份认证与单令牌撤销。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

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

/** @brief 认证接口与会话生命周期的 Qt Test 测试集合。 */
class AuthTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证首次手机号登录创建用户并可用令牌查询身份。
	 */
	void userLoginCreatesAndAuthenticatesUser();
	/**
	 * @brief 验证同一手机号并发首次登录只创建一条用户记录。
	 */
	void concurrentFirstLoginCreatesOneUser();
	/**
	 * @brief 验证管理员密码、停用状态及服务令牌身份边界。
	 */
	void administratorCredentialsAndServiceIdentity();
	/**
	 * @brief 验证注销仅撤销当前令牌而保留其他会话。
	 */
	void logoutRevokesOnlyCurrentToken();
};

namespace {

	/** @brief 认证 API 测试夹具，装配临时数据库、固定时钟和路由。 */
	struct ApiFixture {
		QTemporaryDir directory;					  ///< 用例独立临时目录，夹具销毁时清理数据库文件。
		std::shared_ptr<Backend::Database> database;  ///< 共享数据库入口；每次操作在调用线程创建连接。
		std::shared_ptr<EvCharger::FixedClock> clock; ///< 可注入时钟，统一提供过期判断和充电计量时间。
		Backend::Router router;						  ///< 测试请求分派器或共享路由。

		/**
		 * @brief 初始化隔离认证测试数据库及路由依赖。
		 */
		ApiFixture()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QStringLiteral("service-token"), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			Backend::Config config;
			Backend::registerApiRoutes(router, Backend::ApiDependencies{database, config, clock});
		}

		/**
		 * @brief 构造带 JSON 正文的测试请求并交给路由器分派。
		 * @param method HTTP 请求方法。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

		/**
		 * @brief 发送测试 GET 请求并返回响应。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse get(const QString &path, const QString &token) const {
			Backend::HttpRequest request;
			request.method = QStringLiteral("GET");
			request.path = path;
			request.requestId = QStringLiteral("11111111-1111-4111-8111-111111111111");
			request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			return router.dispatch(request);
		}
	};

	/**
	 * @brief 解析认证测试响应为 JSON 对象。
	 * @param response 待发送或解码的 HTTP 响应。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject responseObject(const Backend::HttpResponse &response) {
		return QJsonDocument::fromJson(response.body).object();
	}

	/**
	 * @brief 执行手机号测试登录并返回访问令牌。
	 * @param fixture 已初始化的认证测试夹具。
	 * @param phone 由 11 个 ASCII 数字组成的手机号文本。
	 * @return 按上述规则生成的文本或字节结果。
	 */
	QString userLogin(ApiFixture &fixture, const QString &phone = QStringLiteral("13800138000")) {
		const Backend::HttpResponse response = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/user/login"), QJsonObject{{QStringLiteral("phone"), phone}});
		if (response.status != 200) {
			return {};
		}
		return responseObject(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
	}

} // namespace

/**
 * @brief 验证首次手机号登录创建用户并可用令牌查询身份。
 */
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

/**
 * @brief 验证同一手机号并发首次登录只创建一条用户记录。
 */
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

/**
 * @brief 验证管理员密码、停用状态及服务令牌身份边界。
 */
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

/**
 * @brief 验证注销仅撤销当前令牌而保留其他会话。
 */
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
