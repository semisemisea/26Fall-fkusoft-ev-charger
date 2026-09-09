/**
 * @file map_tests.cpp
 * @brief 自动化测试：地图接口与提供方错误映射。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

#include "../src/tencent_map_request.h"
#include "backend/api.h"
#include "backend/database.h"
#include "backend/http.h"
#include "backend/map_client.h"
#include "evcharger/clock.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

/** @brief 地图输入校验与可替换客户端的 Qt Test 测试集合。 */
class MapTests : public QObject {
	Q_OBJECT

private slots:
	void signsTencentRequests() {
		QMap<QString, QString> query;
		query.insert(QStringLiteral("region"), QStringLiteral("大连"));
		query.insert(QStringLiteral("key"), QStringLiteral("test-api-key"));
		query.insert(QStringLiteral("address"), QStringLiteral("星海广场 & A+B#%"));
		const QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
		const auto request = Backend::tencentMapRequest(url, query, QStringLiteral("test-secret"));
		const QUrlQuery sent(request.url());
		QCOMPARE(sent.queryItemValue(QStringLiteral("sig")), QStringLiteral("7978f58bf67a9f267331a9243f98f029"));
		QCOMPARE(sent.queryItemValue(QStringLiteral("address"), QUrl::FullyDecoded), QStringLiteral("星海广场 & A+B#%"));
		QCOMPARE(request.rawHeader("x-legacy-url-decode"), QByteArray("no"));
		QVERIFY(!request.url().toString().contains(QStringLiteral("test-secret")));
		QVERIFY(!QUrlQuery(Backend::tencentMapRequest(url, query, {}).url()).hasQueryItem(QStringLiteral("sig")));
	}

	void routeLinksIncludeEndpoints_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<QString>("type");
		QTest::newRow("driving") << QStringLiteral("driving") << QStringLiteral("drive");
		QTest::newRow("walking") << QStringLiteral("walking") << QStringLiteral("walk");
	}

	void routeLinksIncludeEndpoints() {
		QFETCH(QString, mode);
		QFETCH(QString, type);
		const QUrl url(Backend::tencentRouteMapUrl(38.914123, 121.614456, 38.901234, 121.550567, mode));
		QCOMPARE(url.scheme(), QStringLiteral("https"));
		QCOMPARE(url.host(), QStringLiteral("apis.map.qq.com"));
		QCOMPARE(url.path(), QStringLiteral("/uri/v1/routeplan"));
		const QUrlQuery query(url);
		QCOMPARE(query.queryItemValue(QStringLiteral("type")), type);
		QCOMPARE(query.queryItemValue(QStringLiteral("fromcoord")), QStringLiteral("38.914123,121.614456"));
		QCOMPARE(query.queryItemValue(QStringLiteral("tocoord")), QStringLiteral("38.901234,121.550567"));
		// 腾讯导航页仅有坐标时会提示“请检查起终点信息是否有误”。
		QVERIFY2(!query.queryItemValue(QStringLiteral("from"), QUrl::FullyDecoded).trimmed().isEmpty(), "Navigation link is missing its origin name");
		QVERIFY2(!query.queryItemValue(QStringLiteral("to"), QUrl::FullyDecoded).trimmed().isEmpty(), "Navigation link is missing its destination name");
	}

	/**
	 * @brief 注入假地图客户端，验证地址解析、路线及地址附近站点搜索。
	 */
	void servesGeocodeRoutesAndAddressNearby();
	/**
	 * @brief 验证地图输入校验及提供方失败到 API 错误的映射。
	 */
	void mapsValidationAndProviderFailures();
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

	/** @brief 返回可控地图状态的假客户端，避免测试访问外部网络。 */
	class FakeMapClient final : public Backend::MapClient {
	public:
		/// @brief 可由用例改写的地址解析结果，默认返回大连软件园坐标。
		Backend::GeocodeResult geocodeResult{Backend::MapStatus::Success, QStringLiteral("软件园"), QStringLiteral("辽宁省大连市软件园"), 38.889, 121.537};
		/// @brief 可由用例改写的路线结果，含固定距离、耗时和折线。
		Backend::RouteResult routeResult{Backend::MapStatus::Success, 4300, 780, QJsonArray{QJsonArray{38.889, 121.537}, QJsonArray{38.901, 121.55}}, QStringLiteral("https://map.qq.com/route")};
		QString receivedAddress; ///< 假地图客户端最后接收的地址，供断言参数传递。
		QString receivedRegion;	 ///< 假地图客户端最后接收的区域，供断言参数传递。
		QString receivedMode;	 ///< 假地图客户端最后接收的模式，供断言参数传递。

		/**
		 * @brief 记录地址查询参数并返回预设地图结果。
		 * @param address 待解析的地址文本。
		 * @param region 可选的地址搜索区域。
		 * @return 地图查询状态及成功时的规范地址和坐标。
		 */
		Backend::GeocodeResult geocode(const QString &address, const QString &region) override {
			receivedAddress = address;
			receivedRegion = region;
			return geocodeResult;
		}

		/**
		 * @brief 返回预设路线结果并保留测试需要检查的出行模式。
		 * @param mode 地图出行模式。
		 * @return 地图查询状态及成功时的路线距离、耗时、坐标折线和地图链接。
		 */
		Backend::RouteResult route(double, double, double, double, const QString &mode) override {
			receivedMode = mode;
			return routeResult;
		}
	};

	/** @brief 隔离的测试依赖夹具；临时数据库与固定时钟避免用例间状态污染。 */
	struct Fixture {
		QTemporaryDir directory;					  ///< 用例独立临时目录，夹具销毁时清理数据库文件。
		std::shared_ptr<Backend::Database> database;  ///< 共享数据库入口；每次操作在调用线程创建连接。
		std::shared_ptr<EvCharger::FixedClock> clock; ///< 可注入时钟，统一提供过期判断和充电计量时间。
		std::shared_ptr<FakeMapClient> mapClient;	  ///< 可替换地图服务，用于地址与路线查询。
		Backend::Config config;						  ///< 路由使用的业务限制和运行配置快照。
		Backend::Router router;						  ///< 测试请求分派器或共享路由。
		QString adminToken;							  ///< 测试管理员访问令牌。

		/**
		 * @brief 创建临时数据库和固定时钟，注册路由并准备本组测试数据。
		 */
		Fixture()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), clock(std::make_shared<EvCharger::FixedClock>(QDateTime(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC))), mapClient(std::make_shared<FakeMapClient>()) {
			QString error;
			if (!database->initialize(clock->nowUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			config.tencentMapKey = QStringLiteral("test-key");
			Backend::registerApiRoutes(router, Backend::ApiDependencies{database, config, clock, mapClient});
			adminToken = login();
			createStation();
		}

		/**
		 * @brief 组装测试 HTTP 请求及认证、幂等信息并返回响应。
		 * @param method HTTP 请求方法。
		 * @param target 成功时接收经范围校验的数值。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		Backend::HttpResponse send(const QString &method, const QString &target, const QJsonObject &body = {}, const QString &token = {}) const {
			const QUrl url(target);
			Backend::HttpRequest request;
			request.method = method;
			request.path = url.path();
			request.query = QUrlQuery(url);
			request.requestId = QStringLiteral("88888888-8888-4888-8888-888888888888");
			if (!token.isEmpty()) {
				request.headers.insert(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
			}
			if (method == QStringLiteral("POST")) {
				request.headers.insert(QByteArrayLiteral("content-type"), QByteArrayLiteral("application/json"));
				request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
			}
			return router.dispatch(request);
		}

		/**
		 * @brief 执行测试登录并提取令牌；登录失败时终止夹具初始化。
		 * @return 按上述规则生成的文本或字节结果。
		 */
		QString login() {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("admin")}, {QStringLiteral("password"), QStringLiteral("123456")}});
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
		}

		/**
		 * @brief 创建测试站点，供列表、地图或订单场景使用。
		 */
		void createStation() {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), QJsonObject{
																															  {QStringLiteral("name"), QStringLiteral("软件园站")},
																															  {QStringLiteral("latitude"), 38.889},
																															  {QStringLiteral("longitude"), 121.537},
																															  {QStringLiteral("priceFenPerKwh"), 100},
																														  },
														adminToken);
			if (response.status != 201) {
				qFatal("Station setup failed");
			}
		}
	};

} // namespace

/**
 * @brief 注入假地图客户端，验证地址解析、路线及地址附近站点搜索。
 */
void MapTests::servesGeocodeRoutesAndAddressNearby() {
	Fixture fixture;
	const Backend::HttpResponse geocode = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/geocode?address=%20%E8%BD%AF%E4%BB%B6%E5%9B%AD%20&region=%E5%A4%A7%E8%BF%9E"));
	QCOMPARE(geocode.status, 200);
	const QJsonObject location = object(geocode).value(QStringLiteral("data")).toObject();
	QCOMPARE(location.value(QStringLiteral("latitude")).toDouble(), 38.889);
	QCOMPARE(location.value(QStringLiteral("provider")).toString(), QStringLiteral("tencent"));
	QCOMPARE(fixture.mapClient->receivedAddress, QStringLiteral("软件园"));
	QCOMPARE(fixture.mapClient->receivedRegion, QStringLiteral("大连"));

	const Backend::HttpResponse route = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/routes?fromLatitude=38.889&fromLongitude=121.537&toLatitude=38.901&toLongitude=121.55&mode=driving"));
	QCOMPARE(route.status, 200);
	const QJsonObject routeData = object(route).value(QStringLiteral("data")).toObject();
	QCOMPARE(routeData.value(QStringLiteral("distanceM")).toInteger(), qint64(4300));
	QCOMPARE(routeData.value(QStringLiteral("polyline")).toArray().size(), 2);
	QCOMPARE(fixture.mapClient->receivedMode, QStringLiteral("driving"));

	const Backend::HttpResponse nearby = fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/nearby?address=%E8%BD%AF%E4%BB%B6%E5%9B%AD&region=%E5%A4%A7%E8%BF%9E&radiusKm=1"));
	QCOMPARE(nearby.status, 200);
	QCOMPARE(object(nearby).value(QStringLiteral("data")).toArray().size(), 1);
}

/**
 * @brief 验证地图输入校验及提供方失败到 API 错误的映射。
 */
void MapTests::mapsValidationAndProviderFailures() {
	Fixture fixture;
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/geocode?address=%20")).status, 400);
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/routes?fromLatitude=91&fromLongitude=0&toLatitude=0&toLongitude=0&mode=flying")).status, 400);

	fixture.mapClient->geocodeResult.status = Backend::MapStatus::NotFound;
	QCOMPARE(object(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/geocode?address=unknown"))).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("GEOCODE_FAILED"));
	fixture.mapClient->routeResult.status = Backend::MapStatus::ProviderError;
	QCOMPARE(fixture.send(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/routes?fromLatitude=0&fromLongitude=0&toLatitude=1&toLongitude=1&mode=walking")).status, 502);

	fixture.config.tencentMapKey.clear();
	Backend::Router unconfigured;
	Backend::registerApiRoutes(unconfigured, Backend::ApiDependencies{fixture.database, fixture.config, fixture.clock, fixture.mapClient});
	Backend::HttpRequest request;
	request.method = QStringLiteral("GET");
	request.path = QStringLiteral("/api/v1/locations/geocode");
	request.query = QUrlQuery(QStringLiteral("address=test"));
	request.requestId = QStringLiteral("88888888-8888-4888-8888-888888888888");
	QCOMPARE(unconfigured.dispatch(request).status, 503);
}

QTEST_APPLESS_MAIN(MapTests)

#include "map_tests.moc"
