/**
 * @file admin_tests.cpp
 * @brief 自动化测试：管理员统计、订单筛选与用户状态管理。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

#include "backend/api.h"
#include "backend/database.h"
#include "backend/http.h"
#include "backend/security.h"
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

/** @brief 管理员统计、查询和权限边界的 Qt Test 测试集合。 */
class AdminTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证营收汇总、时区日期序列与充电桩统计。
	 */
	void reportsRevenueSeriesAndChargerFacts();
	/**
	 * @brief 验证管理统计的时间、时区和筛选参数校验。
	 */
	void rejectsInvalidDashboardRequests();
	/**
	 * @brief 验证管理员订单列表按条件筛选并保留正确分页数据。
	 */
	void filtersAdministrativeOrders();
	/**
	 * @brief 验证管理员用户资料、冻结解冻及预约取消联动。
	 */
	void managesUsersAndCancelsReservations();
	/**
	 * @brief 验证只读管理员在查询与用户写操作之间的权限边界。
	 */
	void enforcesReadOnlyAdministratorBoundary();
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
		QString userToken;							  ///< 测试普通用户访问令牌。
		qint64 userId = 0;							  ///< 测试用户数据库标识。
		qint64 stationId = 0;						  ///< 本组用例创建的主站点标识。
		qint64 otherStationId = 0;					  ///< 用于验证站点筛选的第二个站点标识。

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

		/**
		 * @brief 组装测试 HTTP 请求及认证、幂等信息并返回响应。
		 * @param method HTTP 请求方法。
		 * @param target 成功时接收经范围校验的数值。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @param idempotencyKey 测试请求的幂等键。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse send(const QString &method, const QString &target, const QJsonObject &body = {}, const QString &token = {}, const QByteArray &idempotencyKey = {}) const {
			const QUrl url(target);
			Backend::HttpRequest request;
			request.method = method;
			request.path = url.path();
			request.query = QUrlQuery(url);
			request.requestId = QStringLiteral("77777777-7777-4777-8777-777777777777");
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			}
			if (!idempotencyKey.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("idempotency-key"), idempotencyKey);
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
		 * @brief 创建测试站点，供列表、地图或订单场景使用。
		 * @param name 待读取或报告的参数名称。
		 * @return 新建测试站点的数据库标识。
		 */
		qint64 createStation(const QString &name) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															  {QStringLiteral("name"), name},
																															  {QStringLiteral("latitude"), 38.9},
																															  {QStringLiteral("longitude"), 121.6},
																															  {QStringLiteral("priceFenPerKwh"), 100},
																														  },
														adminToken);
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		/**
		 * @brief 创建测试充电桩并返回标识，供预约或订单场景使用。
		 * @param ownerStationId 测试充电桩或订单所属站点标识。
		 * @return 新建测试充电桩的数据库标识。
		 */
		qint64 createCharger(qint64 ownerStationId) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/%1/chargers").arg(ownerStationId), QJsonObject{{QStringLiteral("type"), QStringLiteral("fast")}, {QStringLiteral("powerKw"), 60}}, adminToken);
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}

		/**
		 * @brief 插入指定结算时间和金额的订单，构造营收统计数据。
		 * @param ownerStationId 测试充电桩或订单所属站点标识。
		 * @param chargerId 充电桩标识。
		 * @param settledAt 测试订单的结算时间。
		 * @param amountFen 测试金额，单位分。
		 */
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

/**
 * @brief 验证营收汇总、时区日期序列与充电桩统计。
 */
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

/**
 * @brief 验证管理统计的时间、时区和筛选参数校验。
 */
void AdminTests::rejectsInvalidDashboardRequests() {
	Fixture fixture;
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), {}, fixture.userToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue?from=2026-09-01T00%3A00%3A00Z"), {}, fixture.adminToken).status, 400);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue-series?from=2026-09-01T00%3A00%3A00Z&to=2026-09-02T00%3A00%3A00Z&utcOffset=%2B14%3A01"), {}, fixture.adminToken).status, 400);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/charger-status?stationId=0"), {}, fixture.adminToken).status, 400);
}

/**
 * @brief 验证管理员订单列表按条件筛选并保留正确分页数据。
 */
void AdminTests::filtersAdministrativeOrders() {
	Fixture fixture;
	const qint64 charger = fixture.createCharger(fixture.stationId);
	const qint64 otherCharger = fixture.createCharger(fixture.otherStationId);
	fixture.insertSettledOrder(fixture.stationId, charger, QStringLiteral("2026-09-01T09:00:00.000Z"), 100);
	fixture.insertSettledOrder(fixture.otherStationId, otherCharger, QStringLiteral("2026-09-02T09:00:00.000Z"), 200);

	const QString filter = QStringLiteral("?status=settled&userId=%1&stationId=%2&chargerId=%3&from=2026-09-01T00:00:00Z&to=2026-09-02T00:00:00Z").arg(fixture.userId).arg(fixture.stationId).arg(charger);
	const Backend::HttpResponse response = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/orders") + filter, {}, fixture.adminToken);
	QCOMPARE(response.status, 200);
	const QJsonObject envelope = object(response);
	QCOMPARE(envelope.value(QStringLiteral("data")).toArray().size(), 1);
	QCOMPARE(envelope.value(QStringLiteral("meta")).toObject().value(QStringLiteral("total")).toInteger(), qint64(1));
	QCOMPARE(envelope.value(QStringLiteral("data")).toArray().at(0).toObject().value(QStringLiteral("amountFen")).toInteger(), qint64(100));
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/orders"), {}, fixture.userToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/orders?status=invalid"), {}, fixture.adminToken).status, 400);
}

/**
 * @brief 验证管理员用户资料、冻结解冻及预约取消联动。
 */
void AdminTests::managesUsersAndCancelsReservations() {
	Fixture fixture;
	const qint64 charger = fixture.createCharger(fixture.stationId);
	const Backend::HttpResponse reservation = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), QJsonObject{{QStringLiteral("chargerId"), charger}, {QStringLiteral("holdMinutes"), 30}}, fixture.userToken, QByteArrayLiteral("admin-freeze-reservation"));
	QCOMPARE(reservation.status, 201);
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), QJsonObject{{QStringLiteral("amountFen"), 500}}, fixture.userToken, QByteArrayLiteral("admin-wallet")).status, 201);
	fixture.insertSettledOrder(fixture.stationId, charger, QStringLiteral("2026-09-01T09:00:00.000Z"), 100);

	const Backend::HttpResponse users = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users?phone=800&status=active"), {}, fixture.adminToken);
	QCOMPARE(users.status, 200);
	QCOMPARE(object(users).value(QStringLiteral("meta")).toObject().value(QStringLiteral("total")).toInteger(), qint64(1));
	const Backend::HttpResponse detail = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/%1").arg(fixture.userId), {}, fixture.adminToken);
	QCOMPARE(detail.status, 200);
	QCOMPARE(object(detail).value(QStringLiteral("data")).toObject().value(QStringLiteral("recentOrder")).toObject().value(QStringLiteral("amountFen")).toInteger(), qint64(100));
	const Backend::HttpResponse transactions = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/%1/wallet/transactions").arg(fixture.userId), {}, fixture.adminToken);
	QCOMPARE(transactions.status, 200);
	QCOMPARE(object(transactions).value(QStringLiteral("data")).toArray().size(), 1);

	const QJsonObject frozen{{QStringLiteral("status"), QStringLiteral("frozen")}};
	const Backend::HttpResponse freeze = fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/users/%1").arg(fixture.userId), frozen, fixture.adminToken);
	QCOMPARE(freeze.status, 200);
	QCOMPARE(object(freeze).value(QStringLiteral("data")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("frozen"));
	QCOMPARE(fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/users/%1").arg(fixture.userId), frozen, fixture.adminToken).status, 200);

	QString reservationStatus;
	QString occupancyStatus;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT status FROM reservations LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		reservationStatus = query.value(0).toString();
		query.prepare(QStringLiteral("SELECT occupancy_status FROM chargers WHERE id=?"));
		query.addBindValue(charger);
		if (!query.exec() || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		occupancyStatus = query.value(0).toString();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(reservationStatus, QStringLiteral("cancelled"));
	QCOMPARE(occupancyStatus, QStringLiteral("available"));
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users?status=frozen"), {}, fixture.adminToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/999999"), {}, fixture.adminToken).status, 404);
	QCOMPARE(fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/users/%1").arg(fixture.userId), QJsonObject{{QStringLiteral("status"), QStringLiteral("disabled")}}, fixture.adminToken).status, 400);
}

/**
 * @brief 验证只读管理员在查询与用户写操作之间的权限边界。
 */
void AdminTests::enforcesReadOnlyAdministratorBoundary() {
	Fixture fixture;
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		const QByteArray salt = Backend::Security::randomBytes(16);
		QSqlQuery query(database);
		query.prepare(QStringLiteral("INSERT INTO admins(username,display_name,password_salt,password_hash,role,status,created_at,updated_at) VALUES ('auditor','只读管理员',?,?,'ADMIN_READONLY','active',?,?)"));
		query.addBindValue(salt);
		query.addBindValue(Backend::Security::passwordHash(QStringLiteral("secret"), salt));
		query.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		query.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		if (query.exec()) {
			return true;
		}
		*operationError = query.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	const QString readOnlyToken = fixture.login(QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("auditor")}, {QStringLiteral("password"), QStringLiteral("secret")}});

	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), {}, readOnlyToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations"), {}, readOnlyToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/chargers"), {}, readOnlyToken).status, 200);
	QCOMPARE(fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{}, readOnlyToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/orders"), {}, readOnlyToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users"), {}, readOnlyToken).status, 403);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/orders/1"), {}, readOnlyToken).status, 403);
}

QTEST_APPLESS_MAIN(AdminTests)

#include "admin_tests.moc"
