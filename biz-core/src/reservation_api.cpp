/**
 * @file reservation_api.cpp
 * @brief 预约创建、到期清理、查询与取消，维护用户和桩的独占约束。
 */

#include "api_support.h"

#include "resource_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <optional>

namespace Backend {
	namespace {

		/** @brief 经参数校验的页码和页大小，用于 LIMIT/OFFSET 和响应元数据。 */
		struct Pagination {
			int page = 1;	   ///< 从 1 开始的页码。
			int pageSize = 20; ///< 每页记录数。
		};

		/**
		 * @brief 验证调用者为普通用户，并按当前操作要求检查账号是否处于 active 状态。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @param active 是否要求用户账号处于 active 状态。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 通过用户类型及所需账号状态检查的主体；认证失败或不满足权限条件时返回 std::nullopt，并写入 failure。
		 */
		std::optional<Principal> requireUser(const HttpRequest &request, const ApiDependencies &dependencies, bool active, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("user")) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问预约"), {}, request.requestId, 403);
				return std::nullopt;
			}
			if (active && principal->status == QStringLiteral("frozen")) {
				*failure = jsonError(QStringLiteral("USER_FROZEN"), QStringLiteral("用户已被冻结"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		/**
		 * @brief 解析 JSON 正整数，拒绝小数、非数字或非正值。
		 * @param value 待校验、舍入或转换的输入值。
		 * @return 大于零的整数；非数字、小数或非正输入返回 std::nullopt。
		 */
		std::optional<qint64> positiveInteger(const QJsonValue &value) {
			const qint64 integer = value.toInteger(-1);
			return value.isDouble() && integer > 0 ? std::optional<qint64>(integer) : std::nullopt;
		}

		/**
		 * @brief 解析严格大于零的整数资源标识，拒绝非法格式或非正值。
		 * @param raw 待解析的原始文本。
		 * @return 解析出的正整数标识；无法按相应字符串或 JSON 整数规则解析时返回 std::nullopt。
		 */
		std::optional<qint64> positiveId(const QString &raw) {
			bool ok = false;
			const qint64 id = raw.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		/**
		 * @brief 将当前预约查询记录转换为含关联标识、状态和时间的 JSON。
		 * @param query 已定位到目标记录的 SQL 查询。
		 * @return 符合本接口字段约定的 JSON 数据。
		 */
		QJsonObject reservationJson(const QSqlQuery &query) {
			return QJsonObject{
				{QStringLiteral("id"), query.value(0).toLongLong()},
				{QStringLiteral("userId"), query.value(1).toLongLong()},
				{QStringLiteral("stationId"), query.value(2).toLongLong()},
				{QStringLiteral("chargerId"), query.value(3).toLongLong()},
				{QStringLiteral("status"), query.value(4).toString()},
				{QStringLiteral("createdAt"), query.value(5).toString()},
				{QStringLiteral("expiresAt"), query.value(6).toString()},
			};
		}

		/**
		 * @brief 按预约和用户标识加载预约，避免跨用户读取。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param reservationId 预约标识。
		 * @param userId 用户标识；可选类型为空时不限制订单归属。
		 * @param[out] reservation 只有返回 true 且 found 为 true 时才可读取的资源 JSON；指针必须有效。
		 * @param[out] found SQL 成功时写入记录是否存在；true 返回值并不保证找到记录。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return SQL/计量执行成功返回 true（可能 found 为 false）；执行失败返回 false。
		 * @note found 与结果对象指针必须有效；仅在 found 为 true 时读取投影。
		 */
		bool loadReservation(QSqlDatabase &database, qint64 reservationId, qint64 userId, QJsonObject *reservation, bool *found, QString *errorMessage) {
			QSqlQuery query(database);
			query.prepare(QStringLiteral("SELECT id,user_id,station_id,charger_id,status,created_at,expires_at FROM reservations WHERE id=? AND user_id=?"));
			query.addBindValue(reservationId);
			query.addBindValue(userId);
			if (!query.exec()) {
				*errorMessage = query.lastError().text();
				return false;
			}
			if (!query.next()) {
				*found = false;
				return true;
			}
			*reservation = reservationJson(query);
			*found = true;
			return true;
		}

		/**
		 * @brief 解析页码和每页数量，使用接口默认值并拒绝超出范围的输入。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 包含默认值或已校验参数的分页值；非法页码/页大小返回 std::nullopt，并写入 failure。
		 */
		std::optional<Pagination> parsePagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination result;
			auto parse = [&](const QString &name, int fallback, int maximum) -> std::optional<int> {
				if (!request.query.hasQueryItem(name)) {
					return fallback;
				}
				bool ok = false;
				const int value = request.query.queryItemValue(name).toInt(&ok);
				if (!ok || value < 1 || value > maximum) {
					*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("分页参数无效"), {}, request.requestId, 400);
					return std::nullopt;
				}
				return value;
			};
			const auto page = parse(QStringLiteral("page"), 1, std::numeric_limits<int>::max());
			const auto pageSize = parse(QStringLiteral("pageSize"), 20, 100);
			if (!page.has_value() || !pageSize.has_value()) {
				return std::nullopt;
			}
			result.page = *page;
			result.pageSize = *pageSize;
			return result;
		}

		/**
		 * @brief 校验保留时长与幂等键，清理到期预约后原子占用空闲桩并创建预约。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse createReservation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const auto chargerId = positiveInteger(body->value(QStringLiteral("chargerId")));
			const auto holdMinutes = positiveInteger(body->value(QStringLiteral("holdMinutes")));
			QJsonObject details;
			if (!chargerId.has_value()) {
				details.insert(QStringLiteral("chargerId"), QStringLiteral("必须是正整数"));
			}
			if (!holdMinutes.has_value() || *holdMinutes > dependencies.config.maxReservationMinutes) {
				details.insert(QStringLiteral("holdMinutes"), QStringLiteral("超出合法范围"));
			}
			const QByteArray key = request.headers.value(QByteArrayLiteral("idempotency-key"));
			if (key.isEmpty() || key.size() > 200) {
				details.insert(QStringLiteral("Idempotency-Key"), QStringLiteral("必须提供"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("预约参数无效"), details, request.requestId, 400);
			}
			const QJsonObject normalized{{QStringLiteral("chargerId"), *chargerId}, {QStringLiteral("holdMinutes"), *holdMinutes}};
			const QDateTime now = dependencies.clock->nowUtc();
			QJsonObject result;
			IdempotencyResult idempotency;
			QString businessCode;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError) || !checkIdempotency(database, *principal, request, normalized, now, &idempotency, operationError)) {
					database.rollback();
					return false;
				}
				if (idempotency.state != IdempotencyState::Missing) {
					database.rollback();
					return true;
				}
				auto failBusiness = [&](const QString &code, const QString &message, int status) {
					businessCode = code;
					if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, status, idempotencyError(code, message), operationError) || !commitTransaction(database)) {
						database.rollback();
						return false;
					}
					return true;
				};
				QSqlQuery active(database);
				active.prepare(QStringLiteral("SELECT 1 FROM reservations WHERE user_id=? AND status='active' LIMIT 1"));
				active.addBindValue(principal->id);
				if (!active.exec()) {
					*operationError = active.lastError().text();
					database.rollback();
					return false;
				}
				if (active.next()) {
					return failBusiness(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("当前状态不允许创建预约"), 409);
				}
				QSqlQuery openOrder(database);
				openOrder.prepare(QStringLiteral("SELECT 1 FROM orders WHERE user_id=? AND status IN ('charging','awaiting_payment') LIMIT 1"));
				openOrder.addBindValue(principal->id);
				if (!openOrder.exec()) {
					*operationError = openOrder.lastError().text();
					database.rollback();
					return false;
				}
				if (openOrder.next()) {
					return failBusiness(QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("用户已有未完成订单"), 409);
				}
				QSqlQuery charger(database);
				charger.prepare(QStringLiteral("SELECT c.station_id FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=? AND c.deleted_at IS NULL AND s.deleted_at IS NULL AND s.status='active' AND c.occupancy_status='available' AND c.operational_status='online'"));
				charger.addBindValue(*chargerId);
				if (!charger.exec()) {
					*operationError = charger.lastError().text();
					database.rollback();
					return false;
				}
				if (!charger.next()) {
					return failBusiness(QStringLiteral("CHARGER_UNAVAILABLE"), QStringLiteral("电桩当前不可用"), 409);
				}
				const qint64 stationId = charger.value(0).toLongLong();
				QSqlQuery occupy(database);
				occupy.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='reserved',updated_at=? WHERE id=? AND occupancy_status='available' AND operational_status='online' AND deleted_at IS NULL"));
				occupy.addBindValue(toDatabaseTimestamp(now));
				occupy.addBindValue(*chargerId);
				if (!occupy.exec()) {
					*operationError = occupy.lastError().text();
					database.rollback();
					return false;
				}
				if (occupy.numRowsAffected() != 1) {
					return failBusiness(QStringLiteral("CHARGER_UNAVAILABLE"), QStringLiteral("电桩当前不可用"), 409);
				}
				QSqlQuery insert(database);
				insert.prepare(QStringLiteral("INSERT INTO reservations(user_id,station_id,charger_id,status,created_at,expires_at,updated_at) VALUES (?,?,?,'active',?,?,?)"));
				insert.addBindValue(principal->id);
				insert.addBindValue(stationId);
				insert.addBindValue(*chargerId);
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now.addSecs(*holdMinutes * 60)));
				insert.addBindValue(toDatabaseTimestamp(now));
				if (!insert.exec()) {
					*operationError = insert.lastError().text();
					database.rollback();
					return false;
				}
				bool found = false;
				if (!loadReservation(database, insert.lastInsertId().toLongLong(), principal->id, &result, &found, operationError) || !found || !storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 201, result, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (idempotency.state == IdempotencyState::Reused) {
				return jsonError(QStringLiteral("IDEMPOTENCY_KEY_REUSED"), QStringLiteral("幂等键已用于不同请求"), {}, request.requestId, 409);
			}
			if (idempotency.state == IdempotencyState::Replay) {
				return replayIdempotency(idempotency, request.requestId);
			}
			if (businessCode == QStringLiteral("ACTIVE_ORDER_EXISTS")) {
				return jsonError(QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("用户已有未完成订单"), {}, request.requestId, 409);
			}
			if (!businessCode.isEmpty()) {
				const int status = businessCode == QStringLiteral("CHARGER_UNAVAILABLE") ? 409 : 409;
				return jsonError(businessCode, businessCode == QStringLiteral("CHARGER_UNAVAILABLE") ? QStringLiteral("电桩当前不可用") : QStringLiteral("当前状态不允许创建预约"), {}, request.requestId, status);
			}
			return jsonData(result, request.requestId, 201);
		}

		/**
		 * @brief 清理到期预约后分页返回当前用户预约。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse listReservations(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto page = parsePagination(request, &failure);
			if (!page.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("active") && status != QStringLiteral("used") && status != QStringLiteral("cancelled") && status != QStringLiteral("expired")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("预约状态筛选无效"), {}, request.requestId, 400);
			}
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError)) {
					database.rollback();
					return false;
				}
				const QString filter = status.isEmpty() ? QString() : QStringLiteral(" AND status=?");
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE user_id=?") + filter);
				count.addBindValue(principal->id);
				if (!status.isEmpty()) {
					count.addBindValue(status);
				}
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					database.rollback();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,user_id,station_id,charger_id,status,created_at,expires_at FROM reservations WHERE user_id=?") + filter + QStringLiteral(" ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				query.addBindValue(principal->id);
				if (!status.isEmpty()) {
					query.addBindValue(status);
				}
				query.addBindValue(page->pageSize);
				query.addBindValue(static_cast<qint64>(page->page - 1) * page->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					database.rollback();
					return false;
				}
				while (query.next()) {
					items.append(reservationJson(query));
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return jsonData(items, request.requestId, 200, QJsonObject{{QStringLiteral("page"), page->page}, {QStringLiteral("pageSize"), page->pageSize}, {QStringLiteral("total"), total}, {QStringLiteral("hasNext"), static_cast<qint64>(page->page) * page->pageSize < total}});
		}

		/**
		 * @brief 读取当前用户的指定预约并体现到期状态。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse reservationDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto reservationId = positiveId(request.pathParameters.value(QStringLiteral("reservationId")));
			if (!reservationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError) || !loadReservation(database, *reservationId, principal->id, &result, &found, operationError)) {
					database.rollback();
					return false;
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 校验预约归属并取消活动预约、释放桩占用；重复取消返回状态冲突。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse cancelReservation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto reservationId = positiveId(request.pathParameters.value(QStringLiteral("reservationId")));
			if (!reservationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			QJsonObject result;
			bool found = false;
			bool invalidState = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT charger_id,status FROM reservations WHERE id=? AND user_id=?"));
				current.addBindValue(*reservationId);
				current.addBindValue(principal->id);
				if (!current.exec()) {
					*operationError = current.lastError().text();
					database.rollback();
					return false;
				}
				if (!current.next()) {
					database.rollback();
					return true;
				}
				found = true;
				if (current.value(1).toString() != QStringLiteral("active")) {
					invalidState = true;
					database.rollback();
					return true;
				}
				const qint64 chargerId = current.value(0).toLongLong();
				QSqlQuery cancel(database);
				cancel.prepare(QStringLiteral("UPDATE reservations SET status='cancelled',updated_at=? WHERE id=? AND status='active'"));
				cancel.addBindValue(toDatabaseTimestamp(now));
				cancel.addBindValue(*reservationId);
				QSqlQuery release(database);
				release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE id=? AND occupancy_status='reserved'"));
				release.addBindValue(toDatabaseTimestamp(now));
				release.addBindValue(chargerId);
				if (!cancel.exec() || !release.exec() || !loadReservation(database, *reservationId, principal->id, &result, &found, operationError) || !commitTransaction(database)) {
					*operationError = cancel.lastError().text();
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
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			return invalidState ? jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("预约状态不允许取消"), {}, request.requestId, 409) : jsonData(result, request.requestId);
		}

	} // namespace

	/**
	 * @brief 注册预约路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerReservationRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), [dependencies](const HttpRequest &request) { return createReservation(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations"), [dependencies](const HttpRequest &request) { return listReservations(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations/{reservationId}"), [dependencies](const HttpRequest &request) { return reservationDetail(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations/{reservationId}/cancel"), [dependencies](const HttpRequest &request) { return cancelReservation(request, dependencies); });
	}

} // namespace Backend
