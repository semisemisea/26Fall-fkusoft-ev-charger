/**
 * @file user_tests.cpp
 * @brief 自动化测试：用户资料、头像与钱包原子写入。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

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

/** @brief 用户资料及钱包事务的 Qt Test 测试集合。 */
class UserTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证昵称和头像上传、读取、删除及相关输入限制。
	 */
	void managesProfileAndAvatar();
	/**
	 * @brief 验证充值余额与流水同步变化，相同幂等请求不会重复入账。
	 */
	void topUpIsAtomicAndIdempotent();
	/**
	 * @brief 持有数据库写锁，验证锁超时被转换为 HTTP 503。
	 */
	void databaseWriteTimeoutIsServiceUnavailable();
	/**
	 * @brief 注入复合写入失败，验证余额、流水和幂等记录共同回滚。
	 */
	void failedCompositeWriteRollsBack();
};

namespace {

	/**
	 * @brief 解析测试 HTTP 响应正文为 JSON 对象，便于断言信封字段。
	 * @param response 待发送或解码的 HTTP 响应。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject object(const Backend::HttpResponse &response) {
		return QJsonDocument::fromJson(response.body).object();
	}

	/** @brief 隔离的测试依赖夹具；临时数据库与固定时钟避免用例间状态污染。 */
	struct Fixture {
		QTemporaryDir directory;					  ///< 用例独立临时目录，夹具销毁时清理数据库文件。
		std::shared_ptr<Backend::Database> database;  ///< 共享数据库入口；每次操作在调用线程创建连接。
		std::shared_ptr<EvCharger::FixedClock> clock; ///< 可注入时钟，统一提供过期判断和充电计量时间。
		Backend::Router router;						  ///< 测试请求分派器或共享路由。
		QString token;								  ///< 当前测试用户的访问令牌。

		/**
		 * @brief 创建临时数据库和固定时钟，注册路由并准备本组测试数据。
		 * @param busyTimeoutMs SQLite 锁争用等待上限，单位毫秒。
		 */
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

		/**
		 * @brief 构造带 JSON 正文的测试请求并交给路由器分派。
		 * @param method HTTP 请求方法。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param accessToken 用于测试请求认证的访问令牌。
		 * @param idempotencyKey 测试请求的幂等键。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

		/**
		 * @brief 构造当前测试用户的认证请求。
		 * @param method HTTP 请求方法。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse request(const QString &method, const QString &path) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.requestId = QStringLiteral("22222222-2222-4222-8222-222222222222");
			request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			return router.dispatch(request);
		}

		/**
		 * @brief 直接冻结测试用户，用于验证状态权限边界。
		 */
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

/**
 * @brief 验证昵称和头像上传、读取、删除及相关输入限制。
 */
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

/**
 * @brief 验证充值余额与流水同步变化，相同幂等请求不会重复入账。
 */
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
	QString error;
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (query.exec(QStringLiteral("UPDATE users SET balance_fen=0"))) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	const Backend::HttpResponse replayedLimit = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 10'000}}, fixture.token, QByteArrayLiteral("topup-2"));
	QCOMPARE(replayedLimit.status, 422);
	QCOMPARE(object(replayedLimit).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("BALANCE_LIMIT_EXCEEDED"));
	fixture.freezeUser();
	const Backend::HttpResponse frozenTopUp = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 100}}, fixture.token, QByteArrayLiteral("topup-3"));
	QCOMPARE(frozenTopUp.status, 201);
}

/**
 * @brief 持有数据库写锁，验证锁超时被转换为 HTTP 503。
 */
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

/**
 * @brief 注入复合写入失败，验证余额、流水和幂等记录共同回滚。
 */
void UserTests::failedCompositeWriteRollsBack() {
	Fixture fixture;
	QString error;
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (query.exec(QStringLiteral("CREATE TRIGGER fail_topup BEFORE INSERT ON wallet_transactions WHEN NEW.type='top_up' BEGIN SELECT RAISE(ABORT,'forced failure'); END"))) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));

	const Backend::HttpResponse failed = fixture.json(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 500}}, fixture.token, QByteArrayLiteral("rollback-topup"));
	QCOMPARE(failed.status, 500);
	QVERIFY(!failed.body.contains(QByteArrayLiteral("forced failure")));

	qint64 balance = -1;
	qint64 transactionCount = -1;
	qint64 idempotencyCount = -1;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT balance_fen FROM users LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		balance = query.value(0).toLongLong();
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		transactionCount = query.value(0).toLongLong();
		if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM idempotency_records WHERE idempotency_key='rollback-topup'")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		idempotencyCount = query.value(0).toLongLong();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(balance, qint64(0));
	QCOMPARE(transactionCount, qint64(0));
	QCOMPARE(idempotencyCount, qint64(0));
}

QTEST_APPLESS_MAIN(UserTests)

#include "user_tests.moc"
