#include "api_support.h"

#include "backend/map_client.h"
#include "evcharger/validation.h"

#include <cmath>
#include <optional>

namespace Backend {
	namespace {

		HttpResponse mapFailure(MapStatus status, const QString &notFoundCode, const QString &requestId) {
			if (status == MapStatus::NotFound) {
				const QString message = notFoundCode == QStringLiteral("GEOCODE_FAILED") ? QStringLiteral("地址解析无结果") : QStringLiteral("未找到可用路线");
				return jsonError(notFoundCode, message, {}, requestId, 422);
			}
			if (status == MapStatus::ProviderError) {
				return jsonError(QStringLiteral("PROVIDER_ERROR"), QStringLiteral("地图供应商响应异常"), {}, requestId, 502);
			}
			return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("地图服务暂不可用"), {}, requestId, 503);
		}

		std::optional<QPair<QString, QString>> parseAddress(const HttpRequest &request, HttpResponse *failure) {
			QString address;
			QString region;
			const QString rawAddress = request.query.queryItemValue(QStringLiteral("address"), QUrl::FullyDecoded);
			if (!request.query.hasQueryItem(QStringLiteral("address")) || !EvCharger::hasTrimmedLength(rawAddress, 1, 200, &address)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("地址参数无效"), QJsonObject{{QStringLiteral("address"), QStringLiteral("去除首尾空白后须为 1 至 200 个字符")}}, request.requestId, 400);
				return std::nullopt;
			}
			if (request.query.hasQueryItem(QStringLiteral("region")) && !EvCharger::hasTrimmedLength(request.query.queryItemValue(QStringLiteral("region"), QUrl::FullyDecoded), 1, 100, &region)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("区域参数无效"), QJsonObject{{QStringLiteral("region"), QStringLiteral("去除首尾空白后须为 1 至 100 个字符")}}, request.requestId, 400);
				return std::nullopt;
			}
			return QPair<QString, QString>{address, region};
		}

		HttpResponse geocode(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto input = parseAddress(request, &failure);
			if (!input.has_value()) {
				return failure;
			}
			if (!dependencies.mapClient || dependencies.config.tencentMapKey.isEmpty()) {
				return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("地图服务未配置"), {}, request.requestId, 503);
			}
			const GeocodeResult result = dependencies.mapClient->geocode(input->first, input->second);
			if (result.status != MapStatus::Success) {
				return mapFailure(result.status, QStringLiteral("GEOCODE_FAILED"), request.requestId);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("address"), input->first},
								{QStringLiteral("latitude"), result.latitude},
								{QStringLiteral("longitude"), result.longitude},
								{QStringLiteral("formattedAddress"), result.formattedAddress},
								{QStringLiteral("provider"), QStringLiteral("tencent")},
							},
							request.requestId);
		}

		std::optional<double> coordinate(const HttpRequest &request, const QString &name, double minimum, double maximum, QJsonObject *details) {
			bool ok = false;
			const double value = request.query.queryItemValue(name).toDouble(&ok);
			if (!request.query.hasQueryItem(name) || !ok || !std::isfinite(value) || value < minimum || value > maximum) {
				details->insert(name, QStringLiteral("坐标超出合法范围"));
				return std::nullopt;
			}
			return value;
		}

		HttpResponse routes(const HttpRequest &request, const ApiDependencies &dependencies) {
			QJsonObject details;
			const auto fromLatitude = coordinate(request, QStringLiteral("fromLatitude"), -90, 90, &details);
			const auto fromLongitude = coordinate(request, QStringLiteral("fromLongitude"), -180, 180, &details);
			const auto toLatitude = coordinate(request, QStringLiteral("toLatitude"), -90, 90, &details);
			const auto toLongitude = coordinate(request, QStringLiteral("toLongitude"), -180, 180, &details);
			const QString mode = request.query.queryItemValue(QStringLiteral("mode"));
			if (mode != QStringLiteral("driving") && mode != QStringLiteral("walking")) {
				details.insert(QStringLiteral("mode"), QStringLiteral("只接受 driving 或 walking"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("路线参数无效"), details, request.requestId, 400);
			}
			if (!dependencies.mapClient || dependencies.config.tencentMapKey.isEmpty()) {
				return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("地图服务未配置"), {}, request.requestId, 503);
			}
			const RouteResult result = dependencies.mapClient->route(*fromLatitude, *fromLongitude, *toLatitude, *toLongitude, mode);
			if (result.status != MapStatus::Success) {
				return mapFailure(result.status, QStringLiteral("ROUTE_NOT_FOUND"), request.requestId);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("mode"), mode},
								{QStringLiteral("distanceM"), result.distanceM},
								{QStringLiteral("durationSec"), result.durationSec},
								{QStringLiteral("polyline"), result.polyline},
								{QStringLiteral("provider"), QStringLiteral("tencent")},
								{QStringLiteral("mapUrl"), result.mapUrl},
							},
							request.requestId);
		}

	} // namespace

	void registerLocationRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/geocode"), [dependencies](const HttpRequest &request) { return geocode(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/routes"), [dependencies](const HttpRequest &request) { return routes(request, dependencies); });
	}

} // namespace Backend
