/**
 * @file station_tests.cpp
 * @brief 自动化测试：站点生命周期、附近搜索与管理权限。 使用 Qt Test 验证正常流程、校验失败与业务边界。
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

#include <memory>

/** @brief 站点生命周期与管理权限的 Qt Test 测试集合。 */
class StationTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证建站、附近查询、站点状态变化及删除的关联业务约束。
	 */
	void managesStationLifecycleAndNearbySearch();
	/**
	 * @brief 验证只读管理员无法执行站点写操作。
	 */
	void readonlyAdministratorCannotWrite();
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
		}

		/**
		 * @brief 组装测试 HTTP 请求及认证、幂等信息并返回响应。
		 * @param method HTTP 请求方法。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @param query 已定位到目标记录的 SQL 查询。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse send(const QString &method, const QString &path, const QJsonObject &body = {}, const QString &token = {}, const QUrlQuery &query = {}) const {
			Backend::HttpRequest request;
			request.method = method;
			request.path = path;
			request.query = query;
			request.requestId = QStringLiteral("33333333-3333-4333-8333-333333333333");
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
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
		 * @param latitude 纬度，单位度。
		 * @param price 测试站点单价，单位分每千瓦时。
		 * @return 新建测试站点的数据库标识。
		 */
		qint64 createStation(const QString &name, double latitude, qint64 price) {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															  {QStringLiteral("name"), name},
																															  {QStringLiteral("latitude"), latitude},
																															  {QStringLiteral("longitude"), 0.0},
																															  {QStringLiteral("priceFenPerKwh"), price},
																														  },
														adminToken);
			if (response.status != 201) {
				qFatal("Station setup failed: %s", response.body.constData());
			}
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toInteger();
		}
	};

} // namespace

/**
 * @brief 验证建站、附近查询、站点状态变化及删除的关联业务约束。
 */
void StationTests::managesStationLifecycleAndNearbySearch() {
	Fixture fixture;
	const qint64 firstId = fixture.createStation(QStringLiteral("  一号站  "), 0.0, 200);
	const qint64 secondId = fixture.createStation(QStringLiteral("二号站"), 0.01, 100);
	const Backend::HttpResponse detail = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/%1").arg(firstId));
	QCOMPARE(detail.status, 200);
	const QJsonObject station = object(detail).value(QStringLiteral("data")).toObject();
	QCOMPARE(station.value(QStringLiteral("name")).toString(), QStringLiteral("一号站"));
	QVERIFY(!station.contains(QStringLiteral("address")));

	QUrlQuery nearbyQuery;
	nearbyQuery.addQueryItem(QStringLiteral("latitude"), QStringLiteral("0"));
	nearbyQuery.addQueryItem(QStringLiteral("longitude"), QStringLiteral("0"));
	nearbyQuery.addQueryItem(QStringLiteral("radiusKm"), QStringLiteral("5"));
	nearbyQuery.addQueryItem(QStringLiteral("sort"), QStringLiteral("price"));
	const QJsonArray nearby = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/nearby"), {}, {}, nearbyQuery)).value(QStringLiteral("data")).toArray();
	QCOMPARE(nearby.size(), 2);
	QCOMPARE(nearby.first().toObject().value(QStringLiteral("id")).toInteger(), secondId);

	// 距离排序随查询中心改变，不受价格排序或建站顺序影响。
	nearbyQuery.removeAllQueryItems(QStringLiteral("sort"));
	nearbyQuery.addQueryItem(QStringLiteral("sort"), QStringLiteral("distance"));
	const QJsonArray byDistance = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/nearby"), {}, {}, nearbyQuery)).value(QStringLiteral("data")).toArray();
	QCOMPARE(byDistance.size(), 2);
	QCOMPARE(byDistance.first().toObject().value(QStringLiteral("id")).toInteger(), firstId);
	nearbyQuery.removeAllQueryItems(QStringLiteral("latitude"));
	nearbyQuery.addQueryItem(QStringLiteral("latitude"), QStringLiteral("0.01"));
	const QJsonArray fromNewLocation = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/nearby"), {}, {}, nearbyQuery)).value(QStringLiteral("data")).toArray();
	QCOMPARE(fromNewLocation.size(), 2);
	QCOMPARE(fromNewLocation.first().toObject().value(QStringLiteral("id")).toInteger(), secondId);

	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery user(database);
		if (!user.exec(QStringLiteral("SELECT id FROM users LIMIT 1")) || !user.next()) {
			*operationError = user.lastError().text();
			return false;
		}
		const qint64 userId = user.value(0).toLongLong();
		QSqlQuery charger(database);
		charger.prepare(QStringLiteral("INSERT INTO chargers(station_id,type,power_w,occupancy_status,operational_status,created_at,updated_at) VALUES (?,'fast',60000,'reserved','online',?,?)"));
		charger.addBindValue(firstId);
		charger.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		charger.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		if (!charger.exec()) {
			*operationError = charger.lastError().text();
			return false;
		}
		QSqlQuery reservation(database);
		reservation.prepare(QStringLiteral("INSERT INTO reservations(user_id,station_id,charger_id,status,created_at,expires_at,updated_at) VALUES (?,?,?,'active',?,?,?)"));
		reservation.addBindValue(userId);
		reservation.addBindValue(firstId);
		reservation.addBindValue(charger.lastInsertId());
		reservation.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		reservation.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc().addSecs(600)));
		reservation.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc()));
		return reservation.exec();
	},
											  &error),
			 qPrintable(error));

	const Backend::HttpResponse inactive = fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/stations/%1").arg(firstId), QJsonObject{{QStringLiteral("status"), QStringLiteral("inactive")}}, fixture.adminToken);
	QCOMPARE(inactive.status, 200);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/%1").arg(firstId)).status, 404);
	QString reservationStatus;
	QString occupancyStatus;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		if (!query.exec(QStringLiteral("SELECT status FROM reservations LIMIT 1")) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		reservationStatus = query.value(0).toString();
		if (!query.exec(QStringLiteral("SELECT occupancy_status FROM chargers WHERE station_id = %1").arg(firstId)) || !query.next()) {
			*operationError = query.lastError().text();
			return false;
		}
		occupancyStatus = query.value(0).toString();
		return true;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(reservationStatus, QStringLiteral("expired"));
	QCOMPARE(occupancyStatus, QStringLiteral("available"));

	QCOMPARE(fixture.send(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/stations/%1").arg(firstId), QJsonObject{{QStringLiteral("status"), QStringLiteral("active")}}, fixture.adminToken).status, 200);
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery reservation(database);
		reservation.prepare(QStringLiteral("UPDATE reservations SET status='active',expires_at=? WHERE station_id=?"));
		reservation.addBindValue(Backend::toDatabaseTimestamp(fixture.clock->nowUtc().addSecs(600)));
		reservation.addBindValue(firstId);
		QSqlQuery charger(database);
		charger.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='reserved' WHERE station_id=?"));
		charger.addBindValue(firstId);
		if (reservation.exec() && charger.exec()) {
			return true;
		}
		*operationError = reservation.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	const Backend::HttpResponse blockedDelete = fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/stations/%1").arg(firstId), {}, fixture.adminToken);
	QCOMPARE(blockedDelete.status, 409);
	QCOMPARE(object(blockedDelete).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("INVALID_STATE_TRANSITION"));
	QVERIFY2(fixture.database->withConnection([](QSqlDatabase &database, QString *operationError) {
		QSqlQuery reservation(database);
		QSqlQuery charger(database);
		if (reservation.exec(QStringLiteral("UPDATE reservations SET status='cancelled'")) && charger.exec(QStringLiteral("UPDATE chargers SET occupancy_status='available'"))) {
			return true;
		}
		*operationError = reservation.lastError().text();
		return false;
	},
											  &error),
			 qPrintable(error));
	QCOMPARE(fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/stations/%1").arg(firstId), {}, fixture.adminToken).status, 204);
	QCOMPARE(fixture.send(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/stations/%1").arg(firstId), {}, fixture.adminToken).status, 204);
	QUrlQuery includeDeleted;
	includeDeleted.addQueryItem(QStringLiteral("includeDeleted"), QStringLiteral("true"));
	const QJsonArray allStations = object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations"), {}, fixture.adminToken, includeDeleted)).value(QStringLiteral("data")).toArray();
	QCOMPARE(allStations.size(), 2);
}

/**
 * @brief 验证只读管理员无法执行站点写操作。
 */
void StationTests::readonlyAdministratorCannotWrite() {
	Fixture fixture;
	const QByteArray salt = Backend::Security::randomBytes(16);
	QString error;
	QVERIFY2(fixture.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
		QSqlQuery query(database);
		query.prepare(QStringLiteral("INSERT INTO admins(username,display_name,password_salt,password_hash,role,status,created_at,updated_at) VALUES ('viewer','只读',?,?,'ADMIN_READONLY','active',?,?)"));
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
	const QString viewerToken = fixture.login(QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("viewer")}, {QStringLiteral("password"), QStringLiteral("secret")}});
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations"), {}, viewerToken).status, 200);
	const Backend::HttpResponse create = fixture.send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															{QStringLiteral("name"), QStringLiteral("无权创建")},
																															{QStringLiteral("latitude"), 0},
																															{QStringLiteral("longitude"), 0},
																															{QStringLiteral("priceFenPerKwh"), 100},
																														},
													  viewerToken);
	QCOMPARE(create.status, 403);
}

QTEST_APPLESS_MAIN(StationTests)

#include "station_tests.moc"
