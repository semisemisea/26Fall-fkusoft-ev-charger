/**
 * @file admin_user_api.cpp
 * @brief 管理员用户查询、冻结解冻及钱包流水接口。
 */

#include "api_support.h"

#include "order_support.h"

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
		 * @brief 验证管理员身份、账号状态及该接口要求的角色权限。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 通过该接口角色和账号状态检查的管理员；认证或授权失败时返回 std::nullopt，并写入 failure。
		 */
		std::optional<Principal> requireAdmin(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("admin") || principal->status != QStringLiteral("active") || principal->role != QStringLiteral("ADMIN")) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权管理用户"), {}, request.requestId, 403);
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
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
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
		 * @brief 按用户 ID 加载统一资料 JSON，区分查询失败和用户不存在。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param userId 用户标识；可选类型为空时不限制订单归属。
		 * @param[out] user 只有返回 true 且 found 为 true 时才可读取的资源 JSON；指针必须有效。
		 * @param[out] found SQL 成功时写入记录是否存在；true 返回值并不保证找到记录。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return SQL/计量执行成功返回 true（可能 found 为 false）；执行失败返回 false。
		 * @note found 与结果对象指针必须有效；仅在 found 为 true 时读取投影。
		 */
		bool loadUser(QSqlDatabase &database, qint64 userId, QJsonObject *user, bool *found, QString *errorMessage) {
			QSqlQuery query(database);
			query.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE id=?"));
			query.addBindValue(userId);
			if (!query.exec()) {
				*errorMessage = query.lastError().text();
				return false;
			}
			if (!query.next()) {
				*found = false;
				return true;
			}
			*user = userJson(query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString());
			*found = true;
			return true;
		}

		/**
		 * @brief 按手机号和状态过滤用户，返回分页资料与总记录数。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse listUsers(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto pagination = parsePagination(request, &failure);
			if (!pagination.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("active") && status != QStringLiteral("frozen")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("用户状态筛选无效"), QJsonObject{{QStringLiteral("status"), QStringLiteral("不支持的状态")}}, request.requestId, 400);
			}
			const QString phone = request.query.queryItemValue(QStringLiteral("phone"), QUrl::FullyDecoded);
			for (const QChar character : phone) {
				if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("手机号筛选无效"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("只能包含 ASCII 数字")}}, request.requestId, 400);
				}
			}

			QString filter;
			QVariantList bindings;
			if (!phone.isEmpty()) {
				filter += QStringLiteral(" AND instr(phone,?)>0");
				bindings.append(phone);
			}
			if (!status.isEmpty()) {
				filter += QStringLiteral(" AND status=?");
				bindings.append(status);
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
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE 1=1") + filter);
				bindFilters(count);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE 1=1") + filter + QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?"));
				bindFilters(query);
				query.addBindValue(pagination->pageSize);
				query.addBindValue(static_cast<qint64>(pagination->page - 1) * pagination->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					items.append(userJson(query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString()));
				}
				return true;
			},
																	   &databaseError);
			return success ? jsonData(items, request.requestId, 200, pageMeta(*pagination, total)) : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 查询指定用户详情并附带最近订单信息。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse userDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			QJsonObject user;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!loadUser(database, *userId, &user, &found, operationError)) {
					return false;
				}
				if (!found) {
					return true;
				}
				QSqlQuery recent(database);
				recent.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=? ORDER BY created_at DESC,id DESC LIMIT 1"));
				recent.addBindValue(*userId);
				if (!recent.exec()) {
					*operationError = recent.lastError().text();
					return false;
				}
				if (!recent.next()) {
					user.insert(QStringLiteral("recentOrder"), QJsonValue(QJsonValue::Null));
					return true;
				}
				QJsonObject order;
				bool orderFound = false;
				if (!loadOrderJson(database, recent.value(0).toLongLong(), *userId, dependencies.clock->nowUtc(), &order, &orderFound, operationError) || !orderFound) {
					return false;
				}
				user.insert(QStringLiteral("recentOrder"), order);
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(user, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 修改用户状态；冻结时处理活动预约并释放对应充电桩。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse updateUser(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QString status = body->value(QStringLiteral("status")).toString();
			if (!body->value(QStringLiteral("status")).isString() || (status != QStringLiteral("active") && status != QStringLiteral("frozen"))) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("用户状态无效"), QJsonObject{{QStringLiteral("status"), QStringLiteral("只接受 active 或 frozen")}}, request.requestId, 400);
			}

			QJsonObject user;
			bool found = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database)) {
					*operationError = database.lastError().text();
					return false;
				}
				QSqlQuery existing(database);
				existing.prepare(QStringLiteral("SELECT 1 FROM users WHERE id=?"));
				existing.addBindValue(*userId);
				if (!existing.exec()) {
					*operationError = existing.lastError().text();
					database.rollback();
					return false;
				}
				if (!existing.next()) {
					database.rollback();
					return true;
				}
				found = true;
				if (status == QStringLiteral("frozen")) {
					QSqlQuery release(database);
					release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE occupancy_status='reserved' AND id IN (SELECT charger_id FROM reservations WHERE user_id=? AND status='active')"));
					release.addBindValue(toDatabaseTimestamp(now));
					release.addBindValue(*userId);
					QSqlQuery cancel(database);
					cancel.prepare(QStringLiteral("UPDATE reservations SET status='cancelled',updated_at=? WHERE user_id=? AND status='active'"));
					cancel.addBindValue(toDatabaseTimestamp(now));
					cancel.addBindValue(*userId);
					if (!release.exec() || !cancel.exec()) {
						*operationError = !release.lastError().text().isEmpty() ? release.lastError().text() : cancel.lastError().text();
						database.rollback();
						return false;
					}
				}
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE users SET status=?,updated_at=? WHERE id=?"));
				update.addBindValue(status);
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(*userId);
				if (!update.exec() || !loadUser(database, *userId, &user, &found, operationError) || !commitTransaction(database)) {
					if (operationError->isEmpty()) {
						*operationError = update.lastError().text();
					}
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(user, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 按查询列顺序转换钱包流水，保留订单关联及变更后余额。
		 * @param query 已定位到目标记录的 SQL 查询。
		 * @return 符合本接口字段约定的 JSON 数据。
		 */
		QJsonObject walletTransactionJson(const QSqlQuery &query) {
			return QJsonObject{
				{QStringLiteral("id"), query.value(0).toLongLong()},
				{QStringLiteral("type"), query.value(1).toString()},
				{QStringLiteral("amountFen"), query.value(2).toLongLong()},
				{QStringLiteral("balanceAfterFen"), query.value(3).toLongLong()},
				{QStringLiteral("createdAt"), query.value(4).toString()},
			};
		}

		/**
		 * @brief 分页读取管理员指定用户的钱包流水。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse userWalletTransactions(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			const auto pagination = parsePagination(request, &failure);
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			if (!pagination.has_value()) {
				return failure;
			}

			QJsonArray items;
			qint64 total = 0;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery user(database);
				user.prepare(QStringLiteral("SELECT 1 FROM users WHERE id=?"));
				user.addBindValue(*userId);
				if (!user.exec()) {
					*operationError = user.lastError().text();
					return false;
				}
				if (!user.next()) {
					return true;
				}
				found = true;
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions WHERE user_id=?"));
				count.addBindValue(*userId);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,type,amount_fen,balance_after_fen,created_at FROM wallet_transactions WHERE user_id=? ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				query.addBindValue(*userId);
				query.addBindValue(pagination->pageSize);
				query.addBindValue(static_cast<qint64>(pagination->page - 1) * pagination->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					items.append(walletTransactionJson(query));
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(items, request.requestId, 200, pageMeta(*pagination, total)) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

	} // namespace

	/**
	 * @brief 注册管理员用户管理路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAdminUserRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users"), [dependencies](const HttpRequest &request) { return listUsers(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/{userId}"), [dependencies](const HttpRequest &request) { return userDetail(request, dependencies); });
		router.add(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/users/{userId}"), [dependencies](const HttpRequest &request) { return updateUser(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/{userId}/wallet/transactions"), [dependencies](const HttpRequest &request) { return userWalletTransactions(request, dependencies); });
	}

} // namespace Backend
