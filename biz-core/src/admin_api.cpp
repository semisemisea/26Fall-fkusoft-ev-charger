/**
 * @file admin_api.cpp
 * @brief 管理员营收、充电桩状态统计及订单查询接口。
 */

#include "api_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"
#include "evcharger/validation.h"
#include "order_support.h"

#include <QDate>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>

#include <limits>
#include <optional>

namespace Backend {
	namespace {

		/**
		 * @brief 验证管理员身份、账号状态及该接口要求的角色权限。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @param allowReadOnly 是否允许只读管理员访问。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 通过该接口角色和账号状态检查的管理员；认证或授权失败时返回 std::nullopt，并写入 failure。
		 */
		std::optional<Principal> requireAdmin(const HttpRequest &request, const ApiDependencies &dependencies, bool allowReadOnly, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			const bool allowedRole = principal->role == QStringLiteral("ADMIN") || (allowReadOnly && principal->role == QStringLiteral("ADMIN_READONLY"));
			if (principal->type != QStringLiteral("admin") || principal->status != QStringLiteral("active") || !allowedRole) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问此管理资源"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		/** @brief 统计查询时间区间，端点归一化后用于数据库筛选。 */
		struct TimeRange {
			std::optional<QDateTime> from; ///< 可选起始时刻，区间包含此端点。
			std::optional<QDateTime> to;   ///< 可选截止时刻，统计区间不包含此端点。
		};

		/** @brief 经参数校验的页码和页大小，用于 LIMIT/OFFSET 和响应元数据。 */
		struct Pagination {
			int page = 1;	   ///< 从 1 开始的页码。
			int pageSize = 20; ///< 每页记录数。
		};

		/**
		 * @brief 解析严格大于零的整数资源标识，拒绝非法格式或非正值。
		 * @param value 待校验、舍入或转换的输入值。
		 * @return 解析出的正整数标识；无法按相应字符串或 JSON 整数规则解析时返回 std::nullopt。
		 */
		std::optional<qint64> positiveId(const QString &value) {
			bool ok = false;
			const qint64 id = value.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		/**
		 * @brief 读取可选查询资源标识，参数存在但非法时写入校验响应。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param name 待读取或报告的参数名称。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 缺省参数返回值为 0 的 optional；有效参数返回正标识；非法参数返回 std::nullopt 并写入 failure。
		 */
		std::optional<qint64> parseOptionalId(const HttpRequest &request, const QString &name, HttpResponse *failure) {
			if (!request.query.hasQueryItem(name)) {
				return qint64(0);
			}
			const auto id = positiveId(request.query.queryItemValue(name));
			if (!id.has_value()) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("ID 筛选参数无效"), QJsonObject{{name, QStringLiteral("必须是正整数")}}, request.requestId, 400);
				return std::nullopt;
			}
			return id;
		}

		/**
		 * @brief 解析页码和每页数量，使用接口默认值并拒绝超出范围的输入。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 包含默认值或已校验参数的分页值；非法页码/页大小返回 std::nullopt，并写入 failure。
		 */
		std::optional<Pagination> parsePagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination pagination;
			auto parse = [&](const QString &name, int fallback, int maximum) -> std::optional<int> {
				if (!request.query.hasQueryItem(name)) {
					return fallback;
				}
				bool ok = false;
				const int value = request.query.queryItemValue(name).toInt(&ok);
				if (!ok || value < 1 || value > maximum) {
					*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("分页参数无效"), QJsonObject{{name, QStringLiteral("超出合法范围")}}, request.requestId, 400);
					return std::nullopt;
				}
				return value;
			};
			const auto page = parse(QStringLiteral("page"), 1, std::numeric_limits<int>::max());
			const auto pageSize = parse(QStringLiteral("pageSize"), 20, 100);
			if (!page.has_value() || !pageSize.has_value()) {
				return std::nullopt;
			}
			pagination.page = *page;
			pagination.pageSize = *pageSize;
			return pagination;
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
		 * @brief 解析统计时间区间，检查显式时区、端点完整性及起止顺序。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param required 是否要求提供完整时间范围。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 已校验区间；可选且未提供时返回两端皆为空的 TimeRange；端点不完整、非法或倒序返回 std::nullopt。
		 */
		std::optional<TimeRange> parseTimeRange(const HttpRequest &request, bool required, HttpResponse *failure) {
			const bool hasFrom = request.query.hasQueryItem(QStringLiteral("from"));
			const bool hasTo = request.query.hasQueryItem(QStringLiteral("to"));
			if (hasFrom != hasTo || (required && !hasFrom)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("时间区间参数无效"), {}, request.requestId, 400);
				return std::nullopt;
			}
			TimeRange range;
			if (!hasFrom) {
				return range;
			}
			range.from = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("from"), QUrl::FullyDecoded));
			range.to = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("to"), QUrl::FullyDecoded));
			if (!range.from.has_value() || !range.to.has_value() || *range.from >= *range.to) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("时间区间参数无效"), {}, request.requestId, 400);
				return std::nullopt;
			}
			return range;
		}

		/**
		 * @brief 解析统计查询中的可选站点标识。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 未指定站点时返回 0；有效筛选返回正标识；非法筛选返回 std::nullopt 并写入 failure。
		 */
		std::optional<qint64> parseStationId(const HttpRequest &request, HttpResponse *failure) {
			if (!request.query.hasQueryItem(QStringLiteral("stationId"))) {
				return qint64(0);
			}
			bool ok = false;
			const qint64 stationId = request.query.queryItemValue(QStringLiteral("stationId")).toLongLong(&ok);
			if (!ok || stationId <= 0) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电站筛选参数无效"), QJsonObject{{QStringLiteral("stationId"), QStringLiteral("必须是正整数")}}, request.requestId, 400);
				return std::nullopt;
			}
			return stationId;
		}

		/**
		 * @brief 校验统计分桶的时区偏移格式并转换为秒。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 有效偏移秒数；格式或偏移范围不符时返回 std::nullopt，并写入 failure。
		 */
		std::optional<int> parseUtcOffset(const HttpRequest &request, HttpResponse *failure) {
			static const QRegularExpression expression(QStringLiteral("^([+-])(\\d{2}):(\\d{2})$"));
			const QString raw = request.query.queryItemValue(QStringLiteral("utcOffset"), QUrl::FullyDecoded);
			const QRegularExpressionMatch match = expression.match(raw);
			if (!match.hasMatch()) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("UTC 偏移无效"), QJsonObject{{QStringLiteral("utcOffset"), QStringLiteral("格式必须为 +HH:MM 或 -HH:MM")}}, request.requestId, 400);
				return std::nullopt;
			}
			const int hours = match.captured(2).toInt();
			const int minutes = match.captured(3).toInt();
			if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("UTC 偏移无效"), QJsonObject{{QStringLiteral("utcOffset"), QStringLiteral("必须在 -14:00 至 +14:00 范围内")}}, request.requestId, 400);
				return std::nullopt;
			}
			const int sign = match.captured(1) == QStringLiteral("-") ? -1 : 1;
			return sign * (hours * 3600 + minutes * 60);
		}

		/**
		 * @brief 按时间范围及可选站点汇总已结算订单的营收。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse revenue(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto range = parseTimeRange(request, false, &failure);
			const auto stationId = parseStationId(request, &failure);
			if (!range.has_value() || !stationId.has_value()) {
				return failure;
			}

			qint64 revenueFen = 0;
			qint64 orderCount = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT COALESCE(SUM(amount_fen),0),COUNT(*) FROM orders WHERE status='settled'");
				if (range->from.has_value()) {
					sql += QStringLiteral(" AND settled_at>=? AND settled_at<?");
				}
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				if (range->from.has_value()) {
					query.addBindValue(toDatabaseTimestamp(*range->from));
					query.addBindValue(toDatabaseTimestamp(*range->to));
				}
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec() || !query.next()) {
					*operationError = query.lastError().text();
					return false;
				}
				revenueFen = query.value(0).toLongLong();
				orderCount = query.value(1).toLongLong();
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("from"), range->from.has_value() ? QJsonValue(toDatabaseTimestamp(*range->from)) : QJsonValue(QJsonValue::Null)},
								{QStringLiteral("to"), range->to.has_value() ? QJsonValue(toDatabaseTimestamp(*range->to)) : QJsonValue(QJsonValue::Null)},
								{QStringLiteral("revenueFen"), revenueFen},
								{QStringLiteral("orderCount"), orderCount},
							},
							request.requestId);
		}

		/**
		 * @brief 按指定 UTC 偏移的自然日聚合已结算营收，填充查询区间的日期桶。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse revenueSeries(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto range = parseTimeRange(request, true, &failure);
			const auto offsetSeconds = parseUtcOffset(request, &failure);
			const auto stationId = parseStationId(request, &failure);
			if (!range.has_value() || !offsetSeconds.has_value() || !stationId.has_value()) {
				return failure;
			}

			const QDate firstDate = range->from->toOffsetFromUtc(*offsetSeconds).date();
			const QDate lastDate = range->to->addMSecs(-1).toOffsetFromUtc(*offsetSeconds).date();
			QMap<QDate, QPair<qint64, qint64>> totals;
			for (QDate date = firstDate; date.isValid() && date <= lastDate; date = date.addDays(1)) {
				totals.insert(date, {0, 0});
			}

			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT settled_at,amount_fen FROM orders WHERE status='settled' AND settled_at>=? AND settled_at<?");
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				query.addBindValue(toDatabaseTimestamp(*range->from));
				query.addBindValue(toDatabaseTimestamp(*range->to));
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					const QDateTime settledAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
					auto &bucket = totals[settledAt.toOffsetFromUtc(*offsetSeconds).date()];
					bucket.first += query.value(1).toLongLong();
					bucket.second += 1;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}

			QJsonArray points;
			for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
				const QDateTime bucketStart(it.key(), QTime(0, 0), Qt::OffsetFromUTC, *offsetSeconds);
				points.append(QJsonObject{
					{QStringLiteral("localDate"), it.key().toString(Qt::ISODate)},
					{QStringLiteral("bucketStart"), toDatabaseTimestamp(bucketStart)},
					{QStringLiteral("bucketEnd"), toDatabaseTimestamp(bucketStart.addDays(1))},
					{QStringLiteral("revenueFen"), it.value().first},
					{QStringLiteral("orderCount"), it.value().second},
				});
			}
			return jsonData(QJsonObject{
								{QStringLiteral("from"), toDatabaseTimestamp(*range->from)},
								{QStringLiteral("to"), toDatabaseTimestamp(*range->to)},
								{QStringLiteral("utcOffset"), request.query.queryItemValue(QStringLiteral("utcOffset"), QUrl::FullyDecoded)},
								{QStringLiteral("points"), points},
							},
							request.requestId);
		}

		/**
		 * @brief 统计未删除充电桩的运行状态及占用状态数量。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse chargerStatus(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto stationId = parseStationId(request, &failure);
			if (!stationId.has_value()) {
				return failure;
			}

			QList<qint64> counts;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT COUNT(*),COALESCE(SUM(occupancy_status='available'),0),COALESCE(SUM(occupancy_status='reserved'),0),COALESCE(SUM(occupancy_status='charging'),0),COALESCE(SUM(operational_status='online'),0),COALESCE(SUM(operational_status='fault'),0),COALESCE(SUM(operational_status='offline'),0) FROM chargers WHERE deleted_at IS NULL");
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec() || !query.next()) {
					*operationError = query.lastError().text();
					return false;
				}
				for (int column = 0; column < 7; ++column) {
					counts.append(query.value(column).toLongLong());
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("total"), counts.at(0)},
								{QStringLiteral("occupancy"), QJsonObject{{QStringLiteral("available"), counts.at(1)}, {QStringLiteral("reserved"), counts.at(2)}, {QStringLiteral("charging"), counts.at(3)}}},
								{QStringLiteral("operational"), QJsonObject{{QStringLiteral("online"), counts.at(4)}, {QStringLiteral("fault"), counts.at(5)}, {QStringLiteral("offline"), counts.at(6)}}},
							},
							request.requestId);
		}

		/**
		 * @brief 校验管理员订单筛选条件，分页返回订单统一投影及总数。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse listAdminOrders(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, false, &failure).has_value()) {
				return failure;
			}
			const auto pagination = parsePagination(request, &failure);
			const auto range = parseTimeRange(request, false, &failure);
			const auto userId = parseOptionalId(request, QStringLiteral("userId"), &failure);
			const auto stationId = parseOptionalId(request, QStringLiteral("stationId"), &failure);
			const auto chargerId = parseOptionalId(request, QStringLiteral("chargerId"), &failure);
			if (!pagination.has_value() || !range.has_value() || !userId.has_value() || !stationId.has_value() || !chargerId.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("charging") && status != QStringLiteral("awaiting_payment") && status != QStringLiteral("settled")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("订单状态筛选无效"), QJsonObject{{QStringLiteral("status"), QStringLiteral("不支持的状态")}}, request.requestId, 400);
			}

			QString filter = QStringLiteral(" WHERE 1=1");
			QVariantList bindings;
			auto addIdFilter = [&](const QString &column, qint64 id) {
				if (id != 0) {
					filter += QStringLiteral(" AND ") + column + QStringLiteral("=?");
					bindings.append(id);
				}
			};
			if (!status.isEmpty()) {
				filter += QStringLiteral(" AND status=?");
				bindings.append(status);
			}
			addIdFilter(QStringLiteral("user_id"), *userId);
			addIdFilter(QStringLiteral("station_id"), *stationId);
			addIdFilter(QStringLiteral("charger_id"), *chargerId);
			if (range->from.has_value()) {
				filter += QStringLiteral(" AND created_at>=? AND created_at<?");
				bindings.append(toDatabaseTimestamp(*range->from));
				bindings.append(toDatabaseTimestamp(*range->to));
			}

			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				auto bindFilters = [&](QSqlQuery &query) {
					for (const QVariant &binding : bindings) {
						query.addBindValue(binding);
					}
				};
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM orders") + filter);
				bindFilters(count);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();

				QSqlQuery ids(database);
				ids.prepare(QStringLiteral("SELECT id FROM orders") + filter + QStringLiteral(" ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				bindFilters(ids);
				ids.addBindValue(pagination->pageSize);
				ids.addBindValue(static_cast<qint64>(pagination->page - 1) * pagination->pageSize);
				if (!ids.exec()) {
					*operationError = ids.lastError().text();
					return false;
				}
				while (ids.next()) {
					QJsonObject order;
					bool found = false;
					if (!loadOrderJson(database, ids.value(0).toLongLong(), std::nullopt, dependencies.clock->nowUtc(), &order, &found, operationError)) {
						return false;
					}
					if (found) {
						items.append(order);
					}
				}
				return true;
			},
																	   &databaseError);
			return success ? jsonData(items, request.requestId, 200, pageMeta(*pagination, total)) : databaseFailure(request.requestId, databaseError);
		}

	} // namespace

	/**
	 * @brief 注册管理统计及订单路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAdminRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), [dependencies](const HttpRequest &request) { return revenue(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue-series"), [dependencies](const HttpRequest &request) { return revenueSeries(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/charger-status"), [dependencies](const HttpRequest &request) { return chargerStatus(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/orders"), [dependencies](const HttpRequest &request) { return listAdminOrders(request, dependencies); });
	}

} // namespace Backend
