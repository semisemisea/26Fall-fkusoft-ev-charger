/**
 * @file station_api.cpp
 * @brief 站点附近搜索及管理员站点生命周期管理接口。
 */

#include "api_support.h"

#include "resource_support.h"

#include "backend/database.h"
#include "backend/map_client.h"
#include "evcharger/clock.h"
#include "evcharger/geo.h"
#include "evcharger/validation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace Backend {
	namespace {

		/** @brief 经参数校验的页码和页大小，用于 LIMIT/OFFSET 和响应元数据。 */
		struct Pagination {
			int page = 1;	   ///< 从 1 开始的页码。
			int pageSize = 20; ///< 每页记录数。
		};

		/**
		 * @brief 验证管理员身份、账号状态及该接口要求的角色权限。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @param write 是否按写操作要求完整管理员权限。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 通过该接口角色和账号状态检查的管理员；认证或授权失败时返回 std::nullopt，并写入 failure。
		 */
		std::optional<Principal> requireAdmin(const HttpRequest &request, const ApiDependencies &dependencies, bool write, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			const bool allowed = principal->type == QStringLiteral("admin") && principal->status == QStringLiteral("active") && (principal->role == QStringLiteral("ADMIN") || (!write && principal->role == QStringLiteral("ADMIN_READONLY")));
			if (!allowed) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权执行此操作"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		/**
		 * @brief 解析严格大于零的整数资源标识，拒绝非法格式或非正值。
		 * @param value 待校验、舍入或转换的输入值。
		 * @return 解析出的正整数标识；无法按相应字符串或 JSON 整数规则解析时返回 std::nullopt。
		 */
		std::optional<qint64> positiveId(const QString &value) {
			bool ok = false;
			const qint64 id = value.toLongLong(&ok);
			if (!ok || id <= 0) {
				return std::nullopt;
			}
			return id;
		}

		/**
		 * @brief 解析页码和每页数量并验证接口允许的范围。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 包含默认值或已校验参数的分页值；非法页码/页大小返回 std::nullopt，并写入 failure。
		 */
		std::optional<Pagination> pagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination result;
			auto value = [&](const QString &name, int fallback, int maximum) -> std::optional<int> {
				if (!request.query.hasQueryItem(name)) {
					return fallback;
				}
				bool ok = false;
				const int parsed = request.query.queryItemValue(name).toInt(&ok);
				if (!ok || parsed < 1 || parsed > maximum) {
					*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("分页参数无效"), QJsonObject{{name, QStringLiteral("超出合法范围")}}, request.requestId, 400);
					return std::nullopt;
				}
				return parsed;
			};
			const auto page = value(QStringLiteral("page"), 1, std::numeric_limits<int>::max());
			const auto pageSize = value(QStringLiteral("pageSize"), 20, 100);
			if (!page.has_value() || !pageSize.has_value()) {
				return std::nullopt;
			}
			result.page = *page;
			result.pageSize = *pageSize;
			return result;
		}

		/**
		 * @brief 构造页码、每页数量和总记录数的分页元数据。
		 * @param pagination 已校验的页码与页大小。
		 * @param total 筛选后的总记录数。
		 * @return 符合本接口字段约定的 JSON 数据。
		 */
		QJsonObject pageMeta(const Pagination &pagination, qint64 total) {
			return QJsonObject{
				{QStringLiteral("page"), pagination.page},
				{QStringLiteral("pageSize"), pagination.pageSize},
				{QStringLiteral("total"), total},
				{QStringLiteral("hasNext"), static_cast<qint64>(pagination.page) * pagination.pageSize < total},
			};
		}

		/**
		 * @brief 返回未删除且启用的公开站点详情。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse publicStation(const HttpRequest &request, const ApiDependencies &dependencies) {
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			if (!stationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			QJsonObject station;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError) || !loadStationJson(database, *stationId, false, false, &station, &found, operationError)) {
					database.rollback();
					return false;
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(station, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 解析坐标或地址，按球面距离筛选半径内站点并分页排序。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse nearbyStations(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto page = pagination(request, &failure);
			if (!page.has_value()) {
				return failure;
			}
			const bool hasLatitude = request.query.hasQueryItem(QStringLiteral("latitude"));
			const bool hasLongitude = request.query.hasQueryItem(QStringLiteral("longitude"));
			const bool hasAddress = request.query.hasQueryItem(QStringLiteral("address"));
			const bool hasRegion = request.query.hasQueryItem(QStringLiteral("region"));
			if ((hasAddress && (hasLatitude || hasLongitude)) || (!hasAddress && !(hasLatitude && hasLongitude)) || (!hasAddress && hasRegion)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("必须提供完整经纬度或地址，且二者不可同时提供"), {}, request.requestId, 400);
			}
			bool latitudeOk = false;
			bool longitudeOk = false;
			bool radiusOk = false;
			double latitude = request.query.queryItemValue(QStringLiteral("latitude")).toDouble(&latitudeOk);
			double longitude = request.query.queryItemValue(QStringLiteral("longitude")).toDouble(&longitudeOk);
			const double radius = request.query.queryItemValue(QStringLiteral("radiusKm")).toDouble(&radiusOk);
			const QString sort = request.query.queryItemValue(QStringLiteral("sort"), QUrl::FullyDecoded).isEmpty()
									 ? QStringLiteral("distance")
									 : request.query.queryItemValue(QStringLiteral("sort"), QUrl::FullyDecoded);
			if (!radiusOk || !std::isfinite(radius) || radius <= 0 || radius > dependencies.config.maxRadiusKm || (sort != QStringLiteral("distance") && sort != QStringLiteral("price"))) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("附近查询参数无效"), {}, request.requestId, 400);
			}
			if (hasAddress) {
				QString address;
				QString region;
				if (!EvCharger::hasTrimmedLength(request.query.queryItemValue(QStringLiteral("address"), QUrl::FullyDecoded), 1, 200, &address) || (hasRegion && !EvCharger::hasTrimmedLength(request.query.queryItemValue(QStringLiteral("region"), QUrl::FullyDecoded), 1, 100, &region))) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("地址参数无效"), {}, request.requestId, 400);
				}
				if (!dependencies.mapClient || dependencies.config.tencentMapKey.isEmpty()) {
					return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("地图服务未配置"), {}, request.requestId, 503);
				}
				const GeocodeResult geocoded = dependencies.mapClient->geocode(address, region);
				if (geocoded.status == MapStatus::NotFound) {
					return jsonError(QStringLiteral("GEOCODE_FAILED"), QStringLiteral("地址解析无结果"), {}, request.requestId, 422);
				}
				if (geocoded.status == MapStatus::ProviderError) {
					return jsonError(QStringLiteral("PROVIDER_ERROR"), QStringLiteral("地图供应商响应异常"), {}, request.requestId, 502);
				}
				if (geocoded.status != MapStatus::Success) {
					return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("地图服务暂不可用"), {}, request.requestId, 503);
				}
				latitude = geocoded.latitude;
				longitude = geocoded.longitude;
				latitudeOk = true;
				longitudeOk = true;
			}
			if (!latitudeOk || !longitudeOk || !std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("附近查询参数无效"), {}, request.requestId, 400);
			}
			/** @brief 附近搜索候选站点，保存排序键与已生成的响应投影。 */
			struct Candidate {
				qint64 id;		  ///< 主体或资源的数据库标识。
				qint64 price;	  ///< 站点单价，单位分每千瓦时。
				double distance;  ///< 相对搜索中心的球面距离，单位千米。
				QJsonObject json; ///< 已经转换的 JSON 数据，供响应或测试断言使用。
			};
			std::vector<Candidate> candidates;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery query(database);
				if (!query.exec(QStringLiteral("SELECT id,latitude,longitude,price_fen_per_kwh FROM stations WHERE status = 'active' AND deleted_at IS NULL"))) {
					*operationError = query.lastError().text();
					database.rollback();
					return false;
				}
				while (query.next()) {
					const double distance = EvCharger::haversineDistanceKm(latitude, longitude, query.value(1).toDouble(), query.value(2).toDouble());
					if (distance > radius) {
						continue;
					}
					QJsonObject station;
					bool found = false;
					if (!loadStationJson(database, query.value(0).toLongLong(), false, false, &station, &found, operationError)) {
						database.rollback();
						return false;
					}
					station.insert(QStringLiteral("distanceKm"), EvCharger::roundToHundredths(distance));
					candidates.push_back(Candidate{query.value(0).toLongLong(), query.value(3).toLongLong(), distance, station});
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			std::sort(candidates.begin(), candidates.end(), [&](const Candidate &left, const Candidate &right) {
				if (sort == QStringLiteral("price") && left.price != right.price) {
					return left.price < right.price;
				}
				if (left.distance != right.distance) {
					return left.distance < right.distance;
				}
				return left.id < right.id;
			});
			QJsonArray result;
			const qsizetype first = qMin<qsizetype>(static_cast<qint64>(page->page - 1) * page->pageSize, candidates.size());
			const qsizetype last = qMin<qsizetype>(first + page->pageSize, candidates.size());
			for (qsizetype index = first; index < last; ++index) {
				result.append(candidates[index].json);
			}
			return jsonData(result, request.requestId, 200, pageMeta(*page, candidates.size()));
		}

		/** @brief 已校验的站点写入字段，供创建和部分更新共用。 */
		struct StationInput {
			QString name;		  ///< 去掉首尾空白的站点名称。
			double latitude = 0;  ///< 纬度，单位度。
			double longitude = 0; ///< 经度，单位度。
			qint64 price = 0;	  ///< 站点单价，单位分每千瓦时。
			QString status;		  ///< 账号或站点的业务状态文本。
		};

		/**
		 * @brief 校验站点名称、经纬度、价格和状态；patch 模式只检查提交的字段。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @param config 业务校验所使用的配置上下限。
		 * @param[out] input 接收请求中已校验的站点字段；patch 下未提交字段由调用方处理。
		 * @param[in,out] details 累积字段级校验失败原因；调用方须提供有效对象。
		 * @param patch 是否允许仅提交部分站点字段。
		 * @return 字段和创建/更新必填条件全部满足返回 true，否则返回 false；details 包含失败字段。
		 */
		bool validateStationFields(const QJsonObject &body, const Config &config, StationInput *input, QJsonObject *details, bool patch) {
			bool any = false;
			auto text = [&](const QString &name, qsizetype maximum, QString *target) {
				if (!body.contains(name)) {
					return;
				}
				any = true;
				if (!body.value(name).isString() || !EvCharger::hasTrimmedLength(body.value(name).toString(), 1, maximum, target)) {
					details->insert(name, QStringLiteral("必须是规定长度的非空字符串"));
				}
			};
			text(QStringLiteral("name"), 100, &input->name);
			auto coordinate = [&](const QString &name, double minimum, double maximum, double *target) {
				if (!body.contains(name)) {
					return;
				}
				any = true;
				if (!body.value(name).isDouble() || !std::isfinite(body.value(name).toDouble()) || body.value(name).toDouble() < minimum || body.value(name).toDouble() > maximum) {
					details->insert(name, QStringLiteral("超出合法范围"));
				} else {
					*target = body.value(name).toDouble();
				}
			};
			coordinate(QStringLiteral("latitude"), -90, 90, &input->latitude);
			coordinate(QStringLiteral("longitude"), -180, 180, &input->longitude);
			if (body.contains(QStringLiteral("priceFenPerKwh"))) {
				any = true;
				input->price = body.value(QStringLiteral("priceFenPerKwh")).toInteger(-1);
				if (!body.value(QStringLiteral("priceFenPerKwh")).isDouble() || input->price <= 0 || input->price > config.maxStationPriceFenPerKwh) {
					details->insert(QStringLiteral("priceFenPerKwh"), QStringLiteral("必须是上限内的正整数"));
				}
			}
			if (body.contains(QStringLiteral("status"))) {
				any = true;
				input->status = body.value(QStringLiteral("status")).toString();
				if (!body.value(QStringLiteral("status")).isString() || (input->status != QStringLiteral("active") && input->status != QStringLiteral("inactive"))) {
					details->insert(QStringLiteral("status"), QStringLiteral("非法状态"));
				}
			}
			if (!patch) {
				for (const QString &required : {QStringLiteral("name"), QStringLiteral("latitude"), QStringLiteral("longitude"), QStringLiteral("priceFenPerKwh")}) {
					if (!body.contains(required)) {
						details->insert(required, QStringLiteral("必填"));
					}
				}
			}
			if (patch && !any) {
				details->insert(QStringLiteral("body"), QStringLiteral("至少提供一个可修改字段"));
			}
			return details->isEmpty();
		}

		/**
		 * @brief 校验站点字段并创建站点，返回完整站点投影。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse createStation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			StationInput input;
			QJsonObject details;
			if (!validateStationFields(*body, dependencies.config, &input, &details, false)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电站参数无效"), details, request.requestId, 400);
			}
			QJsonObject result;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery insert(database);
				insert.prepare(QStringLiteral("INSERT INTO stations(name,latitude,longitude,price_fen_per_kwh,status,created_at,updated_at) VALUES (?,?,?,?,'active',?,?)"));
				insert.addBindValue(input.name);
				insert.addBindValue(input.latitude);
				insert.addBindValue(input.longitude);
				insert.addBindValue(input.price);
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now));
				if (!insert.exec()) {
					*operationError = insert.lastError().text();
					database.rollback();
					return false;
				}
				bool found = false;
				if (!loadStationJson(database, insert.lastInsertId().toLongLong(), true, false, &result, &found, operationError) || !found || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			return success ? jsonData(result, request.requestId, 201) : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 返回管理员可见的站点详情。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse adminStationDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, false, &failure).has_value()) {
				return failure;
			}
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			if (!stationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			QJsonObject station;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError) || !loadStationJson(database, *stationId, true, false, &station, &found, operationError)) {
					database.rollback();
					return false;
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(station, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 按管理筛选条件分页列出站点及总数。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse listAdminStations(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, false, &failure).has_value()) {
				return failure;
			}
			const auto page = pagination(request, &failure);
			if (!page.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("active") && status != QStringLiteral("inactive")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电站状态筛选无效"), {}, request.requestId, 400);
			}
			bool includeDeleted = false;
			if (request.query.hasQueryItem(QStringLiteral("includeDeleted"))) {
				const QString raw = request.query.queryItemValue(QStringLiteral("includeDeleted"));
				if (raw != QStringLiteral("true") && raw != QStringLiteral("false")) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("includeDeleted 必须是布尔值"), {}, request.requestId, 400);
				}
				includeDeleted = raw == QStringLiteral("true");
			}
			const QString name = request.query.queryItemValue(QStringLiteral("name"));
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError)) {
					database.rollback();
					return false;
				}
				QString where = QStringLiteral(" WHERE 1=1");
				QVariantList bindings;
				if (!includeDeleted) {
					where += QStringLiteral(" AND deleted_at IS NULL");
				}
				if (!status.isEmpty()) {
					where += QStringLiteral(" AND status = ?");
					bindings.append(status);
				}
				if (!name.isEmpty()) {
					where += QStringLiteral(" AND instr(name, ?) > 0");
					bindings.append(name);
				}
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM stations") + where);
				for (const QVariant &binding : bindings) {
					count.addBindValue(binding);
				}
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					database.rollback();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery ids(database);
				ids.prepare(QStringLiteral("SELECT id FROM stations") + where + QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?"));
				for (const QVariant &binding : bindings) {
					ids.addBindValue(binding);
				}
				ids.addBindValue(page->pageSize);
				ids.addBindValue(static_cast<qint64>(page->page - 1) * page->pageSize);
				if (!ids.exec()) {
					*operationError = ids.lastError().text();
					database.rollback();
					return false;
				}
				while (ids.next()) {
					QJsonObject station;
					bool found = false;
					if (!loadStationJson(database, ids.value(0).toLongLong(), true, includeDeleted, &station, &found, operationError)) {
						database.rollback();
						return false;
					}
					if (found) {
						items.append(station);
					}
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			return success ? jsonData(items, request.requestId, 200, pageMeta(*page, total)) : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 修改站点属性，停用时同步将活动预约设为 expired 并释放占用。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse updateStation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			if (!stationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			StationInput provided;
			QJsonObject details;
			if (!validateStationFields(*body, dependencies.config, &provided, &details, true)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电站参数无效"), details, request.requestId, 400);
			}
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT name,latitude,longitude,price_fen_per_kwh,status FROM stations WHERE id = ? AND deleted_at IS NULL"));
				current.addBindValue(*stationId);
				if (!current.exec()) {
					*operationError = current.lastError().text();
					database.rollback();
					return false;
				}
				if (!current.next()) {
					found = false;
					database.rollback();
					return true;
				}
				found = true;
				const QString name = body->contains(QStringLiteral("name")) ? provided.name : current.value(0).toString();
				const double latitude = body->contains(QStringLiteral("latitude")) ? provided.latitude : current.value(1).toDouble();
				const double longitude = body->contains(QStringLiteral("longitude")) ? provided.longitude : current.value(2).toDouble();
				const qint64 price = body->contains(QStringLiteral("priceFenPerKwh")) ? provided.price : current.value(3).toLongLong();
				const QString status = body->contains(QStringLiteral("status")) ? provided.status : current.value(4).toString();
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE stations SET name=?,latitude=?,longitude=?,price_fen_per_kwh=?,status=?,updated_at=? WHERE id=?"));
				update.addBindValue(name);
				update.addBindValue(latitude);
				update.addBindValue(longitude);
				update.addBindValue(price);
				update.addBindValue(status);
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(*stationId);
				if (!update.exec()) {
					*operationError = update.lastError().text();
					database.rollback();
					return false;
				}
				if (status == QStringLiteral("inactive")) {
					QSqlQuery release(database);
					release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE station_id=? AND occupancy_status='reserved' AND id IN (SELECT charger_id FROM reservations WHERE status='active')"));
					release.addBindValue(toDatabaseTimestamp(now));
					release.addBindValue(*stationId);
					QSqlQuery expire(database);
					expire.prepare(QStringLiteral("UPDATE reservations SET status='expired',updated_at=? WHERE station_id=? AND status='active'"));
					expire.addBindValue(toDatabaseTimestamp(now));
					expire.addBindValue(*stationId);
					if (!release.exec() || !expire.exec()) {
						*operationError = expire.lastError().text();
						database.rollback();
						return false;
					}
				}
				if (!loadStationJson(database, *stationId, true, false, &result, &found, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 在检查预约与订单冲突后软删除站点及其充电桩。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse deleteStation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			if (!stationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			bool found = false;
			bool blocked = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery station(database);
				station.prepare(QStringLiteral("SELECT deleted_at FROM stations WHERE id=?"));
				station.addBindValue(*stationId);
				if (!station.exec()) {
					*operationError = station.lastError().text();
					database.rollback();
					return false;
				}
				if (!station.next()) {
					database.rollback();
					return true;
				}
				found = true;
				if (!station.isNull(0)) {
					database.rollback();
					return true;
				}
				QSqlQuery conflict(database);
				conflict.prepare(QStringLiteral("SELECT 1 FROM reservations WHERE station_id=? AND status='active' UNION ALL SELECT 1 FROM orders WHERE station_id=? AND status='charging' LIMIT 1"));
				conflict.addBindValue(*stationId);
				conflict.addBindValue(*stationId);
				if (!conflict.exec()) {
					*operationError = conflict.lastError().text();
					database.rollback();
					return false;
				}
				if (conflict.next()) {
					blocked = true;
					database.rollback();
					return true;
				}
				QSqlQuery chargers(database);
				chargers.prepare(QStringLiteral("UPDATE chargers SET deleted_at=?,updated_at=? WHERE station_id=? AND deleted_at IS NULL"));
				chargers.addBindValue(toDatabaseTimestamp(now));
				chargers.addBindValue(toDatabaseTimestamp(now));
				chargers.addBindValue(*stationId);
				QSqlQuery remove(database);
				remove.prepare(QStringLiteral("UPDATE stations SET deleted_at=?,updated_at=? WHERE id=?"));
				remove.addBindValue(toDatabaseTimestamp(now));
				remove.addBindValue(toDatabaseTimestamp(now));
				remove.addBindValue(*stationId);
				if (!chargers.exec() || !remove.exec() || !commitTransaction(database)) {
					*operationError = remove.lastError().text();
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (!found) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			if (blocked) {
				return jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("电站存在有效预约或正在充电订单"), {}, request.requestId, 409);
			}
			return HttpResponse{204, {}, {}, {}};
		}

	} // namespace

	/**
	 * @brief 注册站点路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerStationRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/nearby"), [dependencies](const HttpRequest &request) { return nearbyStations(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/{stationId}"), [dependencies](const HttpRequest &request) { return publicStation(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations"), [dependencies](const HttpRequest &request) { return listAdminStations(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations"), [dependencies](const HttpRequest &request) { return createStation(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations/{stationId}"), [dependencies](const HttpRequest &request) { return adminStationDetail(request, dependencies); });
		router.add(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/stations/{stationId}"), [dependencies](const HttpRequest &request) { return updateStation(request, dependencies); });
		router.add(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/stations/{stationId}"), [dependencies](const HttpRequest &request) { return deleteStation(request, dependencies); });
	}

} // namespace Backend
