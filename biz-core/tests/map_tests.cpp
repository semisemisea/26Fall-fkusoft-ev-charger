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

class MapTests : public QObject {
	Q_OBJECT

private slots:
	void servesGeocodeRoutesAndAddressNearby();
	void mapsValidationAndProviderFailures();
};

namespace {

	QJsonObject object(const Backend::HttpResponse &response) {
		return QJsonDocument::fromJson(response.body).object();
	}

	class FakeMapClient final : public Backend::MapClient {
	public:
		Backend::GeocodeResult geocodeResult{Backend::MapStatus::Success, QStringLiteral("软件园"), QStringLiteral("辽宁省大连市软件园"), 38.889, 121.537};
		Backend::RouteResult routeResult{Backend::MapStatus::Success, 4300, 780, QJsonArray{QJsonArray{38.889, 121.537}, QJsonArray{38.901, 121.55}}, QStringLiteral("https://map.qq.com/route")};
		QString receivedAddress;
		QString receivedRegion;
		QString receivedMode;

		Backend::GeocodeResult geocode(const QString &address, const QString &region) override {
			receivedAddress = address;
			receivedRegion = region;
			return geocodeResult;
		}

		Backend::RouteResult route(double, double, double, double, const QString &mode) override {
			receivedMode = mode;
			return routeResult;
		}
	};

	struct Fixture {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<EvCharger::FixedClock> clock;
		std::shared_ptr<FakeMapClient> mapClient;
		Backend::Config config;
		Backend::Router router;
		QString adminToken;

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

		QString login() {
			const Backend::HttpResponse response = send(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/admin/login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("admin")}, {QStringLiteral("password"), QStringLiteral("123456")}});
			return object(response).value(QStringLiteral("data")).toObject().value(QStringLiteral("accessToken")).toString();
		}

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
