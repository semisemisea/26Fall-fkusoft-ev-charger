/**
 * @file reservation_tests.cpp
 * @brief 自动化测试：预约独占、过期、取消与幂等。 使用 Qt Test 验证正常流程、校验失败与业务边界。
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

#include <future>
#include <memory>

/** @brief 预约状态流转与并发独占的 Qt Test 测试集合。 */
class ReservationTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证预约创建、幂等重放、到期释放和取消状态流转。
	 */
	void createsReplaysExpiresAndCancels();
	/**
	 * @brief 并发预约同一桩，验证只有一个请求成功占用。
	 */
	void concurrentReservationHasSingleWinner();
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
		QString adminToken;							  ///< 测试管理员访问令牌。
		QString firstUserToken;						  ///< 并发场景中第一个用户的访问令牌。
		QString secondUserToken;					  ///< 并发场景中第二个用户的访问令牌。
		qint64 stationId = 0;						  ///< 本组用例创建的主站点标识。

		/**
		 * @brief 创建临时数据库和固定时钟，注册路由并准备本组测试数据。
		 */
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

		/**
		 * @brief 组装测试 HTTP 请求及认证、幂等信息并返回响应。
		 * @param method HTTP 请求方法。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @param key 地图服务密钥。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

		/**
		 * @brief 执行测试登录并提取令牌；登录失败时终止夹具初始化。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @return 按上述规则生成的文本或字节结果。
		 */
		QString login(const QString &path, const QJsonObject &body) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), path, body);
			if (response.status != 200) {
				qFatal("Login setup failed");
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
		}

		/**
		 * @brief 创建测试充电桩并返回标识，供预约或订单场景使用。
		 * @return 新建测试充电桩的数据库标识。
		 */
		qint64 createCharger() {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(stationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 60}}, adminToken);
			if (response.status != 201) {
				qFatal("Charger setup failed");
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		/**
		 * @brief 发送指定桩和保留分钟数的预约请求，用于幂等及并发测试。
		 * @param chargerId 充电桩标识。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @param key 地图服务密钥。
		 * @param minutes 预约保留分钟数。
		 * @param unknown 是否附加未知字段以验证规范化行为。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse reserve(qint64 chargerId, const QString &token, const QByteArray &key, int minutes = 30, bool unknown = false) const {
			QJsonObject body{{QStringLiteral("chargerId"), chargerId}, {QStringLiteral("holdMinutes"), minutes}};
			if (unknown) {
				body.insert(QStringLiteral("ignored"), true);
			}
			return send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), body, token, key);
		}
	};

} // namespace

/**
 * @brief 验证预约创建、幂等重放、到期释放和取消状态流转。
 */
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

/**
 * @brief 并发预约同一桩，验证只有一个请求成功占用。
 */
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
