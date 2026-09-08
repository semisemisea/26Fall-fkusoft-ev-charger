/**
 * @file location_api.cpp
 * @brief 地址解析与路线查询的参数校验和地图错误映射。
 */

#include "api_support.h"

#include "backend/map_client.h"
#include "evcharger/validation.h"

#include <cmath>
#include <optional>

namespace Backend {
	namespace {

		/**
		 * @brief 将地图未找到、提供方错误及不可用状态转换为对应 HTTP 错误。
		 * @param status 地图查询结果分类，用于选择 HTTP 状态和业务错误码。
		 * @param notFoundCode 地图结果不存在时使用的业务错误码。
		 * @param requestId 本次请求关联标识。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

		/**
		 * @brief 校验地址和可选区域文本，输出已去除首尾空白的查询参数。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 已修剪的地址和区域组成的二元组；字段类型或长度非法时返回 std::nullopt，并写入 failure。
		 */
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

		/**
		 * @brief 将地址和区域解析为规范地址及经纬度，保留地图提供方状态。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

		/**
		 * @brief 读取并校验坐标查询值的有限性与允许范围。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param name 待读取或报告的参数名称。
		 * @param minimum 允许的最小值，包含边界。
		 * @param maximum 允许的最大值，包含边界。
		 * @param[in,out] details 累积字段级校验失败原因；调用方须提供有效对象。
		 * @return 有效范围内的有限坐标数；缺失、解析失败或越界时返回 std::nullopt，并补充 details。
		 */
		std::optional<double> coordinate(const HttpRequest &request, const QString &name, double minimum, double maximum, QJsonObject *details) {
			bool ok = false;
			const double value = request.query.queryItemValue(name).toDouble(&ok);
			if (!request.query.hasQueryItem(name) || !ok || !std::isfinite(value) || value < minimum || value > maximum) {
				details->insert(name, QStringLiteral("坐标超出合法范围"));
				return std::nullopt;
			}
			return value;
		}

		/**
		 * @brief 校验起终点坐标和出行模式后请求地图路线。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
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

	/**
	 * @brief 注册地址和路线路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerLocationRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/geocode"), [dependencies](const HttpRequest &request) { return geocode(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/locations/routes"), [dependencies](const HttpRequest &request) { return routes(request, dependencies); });
	}

} // namespace Backend
