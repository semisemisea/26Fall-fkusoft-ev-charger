/**
 * @file auth_api.cpp
 * @brief 用户与管理员登录、当前身份查询及单令牌注销接口。
 */
#include "evcharger/logging.h"

#include "api_support.h"

#include "backend/database.h"
#include "backend/security.h"
#include "evcharger/clock.h"
#include "evcharger/validation.h"

#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(backendAuthentication, "evcharger.backend.authentication", QtInfoMsg)

namespace Backend {
	namespace {

		/// @brief 普通访问令牌有效期为七天，单位秒。
		constexpr qint64 tokenLifetimeSeconds = 7 * 24 * 60 * 60;

		/**
		 * @brief 创建单字段参数校验错误，保留请求 ID 用于关联响应。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param field 发生校验错误的字段名。
		 * @param reason 字段校验失败原因。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse validationError(const HttpRequest &request, const QString &field, const QString &reason) {
			return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("请求参数无效"), QJsonObject{{field, reason}}, request.requestId, 400);
		}

		/**
		 * @brief 保存访问令牌摘要、主体与角色，并设置七天有效期。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
		 * @param principalType 令牌所属主体类型。
		 * @param principalId 令牌所属主体标识。
		 * @param role 身份对应的授权角色。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 * @return 带七天有效期的令牌记录插入成功返回 true，SQL 插入失败返回 false。
		 */
		bool insertAccessToken(QSqlDatabase &database,
							   const QString &token,
							   const QString &principalType,
							   qint64 principalId,
							   const QString &role,
							   const QDateTime &nowUtc) {
			QSqlQuery query(database);
			query.prepare(QStringLiteral("INSERT INTO access_tokens(token_hash,principal_type,principal_id,role,created_at,expires_at) VALUES (?,?,?,?,?,?)"));
			query.addBindValue(Security::tokenHash(token));
			query.addBindValue(principalType);
			query.addBindValue(principalId);
			query.addBindValue(role);
			query.addBindValue(toDatabaseTimestamp(nowUtc));
			query.addBindValue(toDatabaseTimestamp(nowUtc.addSecs(tokenLifetimeSeconds)));
			return query.exec();
		}

		/**
		 * @brief 以手机号创建或查找用户并签发令牌；冻结用户仅在仍有未完成订单时允许登录。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse userLogin(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAuthentication, nullptr) << "Handling userLogin" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QJsonValue phoneValue = body->value(QStringLiteral("phone"));
			if (!phoneValue.isString() || !EvCharger::isPhoneNumber(phoneValue.toString())) {
				return validationError(request, QStringLiteral("phone"), QStringLiteral("必须恰好包含 11 个 ASCII 数字"));
			}
			const QString phone = phoneValue.toString();
			const QDateTime now = dependencies.clock->nowUtc();
			const QString timestamp = toDatabaseTimestamp(now);
			const QString token = Security::generateAccessToken();
			QJsonObject user;
			HttpResponse businessFailure;
			bool hasBusinessFailure = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database)) {
					*operationError = database.lastError().text();
					return false;
				}
				QSqlQuery create(database);
				create.prepare(QStringLiteral("INSERT OR IGNORE INTO users(phone,nickname,balance_fen,status,created_at,updated_at) VALUES (?,?,0,'active',?,?)"));
				create.addBindValue(phone);
				create.addBindValue(QStringLiteral("用户") + phone.right(4));
				create.addBindValue(timestamp);
				create.addBindValue(timestamp);
				if (!create.exec()) {
					*operationError = create.lastError().text();
					database.rollback();
					return false;
				}
				QSqlQuery select(database);
				select.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE phone = ?"));
				select.addBindValue(phone);
				if (!select.exec() || !select.next()) {
					*operationError = select.lastError().text();
					database.rollback();
					return false;
				}
				const qint64 userId = select.value(0).toLongLong();
				const QString status = select.value(5).toString();
				if (status == QStringLiteral("frozen")) {
					QSqlQuery openOrder(database);
					openOrder.prepare(QStringLiteral("SELECT 1 FROM orders WHERE user_id = ? AND status IN ('charging','awaiting_payment') LIMIT 1"));
					openOrder.addBindValue(userId);
					if (!openOrder.exec()) {
						*operationError = openOrder.lastError().text();
						database.rollback();
						return false;
					}
					if (!openOrder.next()) {
						database.rollback();
						hasBusinessFailure = true;
						businessFailure = jsonError(QStringLiteral("USER_FROZEN"), QStringLiteral("用户已被冻结"), {}, request.requestId, 403);
						return true;
					}
				}
				user = userJson(userId, select.value(1).toString(), select.value(2).toString(), select.value(3).toBool(), select.value(4).toLongLong(), status, select.value(6).toString());
				if (!insertAccessToken(database, token, QStringLiteral("user"), userId, QStringLiteral("USER"), now)) {
					*operationError = database.lastError().text();
					database.rollback();
					return false;
				}
				if (!commitTransaction(database)) {
					*operationError = database.lastError().text();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (hasBusinessFailure) {
				return businessFailure;
			}
			EV_LOG_INFO(backendAuthentication, nullptr) << "User login succeeded" << "requestId=" << request.requestId << "userId=" << user.value(QStringLiteral("id")).toInteger();
			return jsonData(QJsonObject{
								{QStringLiteral("accessToken"), token},
								{QStringLiteral("tokenType"), QStringLiteral("Bearer")},
								{QStringLiteral("expiresIn"), tokenLifetimeSeconds},
								{QStringLiteral("user"), user},
							},
							request.requestId);
		}

		/**
		 * @brief 校验管理员用户名、加盐密码摘要和账号状态后签发访问令牌。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse adminLogin(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAuthentication, nullptr) << "Handling adminLogin" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QJsonValue usernameValue = body->value(QStringLiteral("username"));
			const QJsonValue passwordValue = body->value(QStringLiteral("password"));
			if (!usernameValue.isString() || usernameValue.toString().isEmpty()) {
				return validationError(request, QStringLiteral("username"), QStringLiteral("必须是非空字符串"));
			}
			if (!passwordValue.isString() || passwordValue.toString().isEmpty()) {
				return validationError(request, QStringLiteral("password"), QStringLiteral("必须是非空字符串"));
			}
			const QDateTime now = dependencies.clock->nowUtc();
			const QString token = Security::generateAccessToken();
			QJsonObject admin;
			HttpResponse credentialFailure;
			bool rejected = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery select(database);
				select.prepare(QStringLiteral("SELECT id,username,display_name,password_salt,password_hash,role,status FROM admins WHERE username = ?"));
				select.addBindValue(usernameValue.toString());
				if (!select.exec()) {
					*operationError = select.lastError().text();
					return false;
				}
				if (!select.next() || !Security::verifyPassword(passwordValue.toString(), select.value(3).toByteArray(), select.value(4).toByteArray())) {
					rejected = true;
					credentialFailure = jsonError(QStringLiteral("INVALID_CREDENTIALS"), QStringLiteral("用户名或密码错误"), {}, request.requestId, 401);
					return true;
				}
				if (select.value(6).toString() == QStringLiteral("disabled")) {
					rejected = true;
					credentialFailure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("管理员账号已停用"), {}, request.requestId, 403);
					return true;
				}
				const qint64 adminId = select.value(0).toLongLong();
				const QString role = select.value(5).toString();
				if (!beginTransaction(database) || !insertAccessToken(database, token, QStringLiteral("admin"), adminId, role, now) || !commitTransaction(database)) {
					*operationError = database.lastError().text();
					database.rollback();
					return false;
				}
				admin = QJsonObject{
					{QStringLiteral("id"), adminId},
					{QStringLiteral("username"), select.value(1).toString()},
					{QStringLiteral("displayName"), select.value(2).toString()},
					{QStringLiteral("role"), role},
					{QStringLiteral("status"), select.value(6).toString()},
				};
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (rejected) {
				return credentialFailure;
			}
			EV_LOG_INFO(backendAuthentication, nullptr) << "Administrator login succeeded" << "requestId=" << request.requestId << "adminId=" << admin.value(QStringLiteral("id")).toInteger();
			return jsonData(QJsonObject{
								{QStringLiteral("accessToken"), token},
								{QStringLiteral("tokenType"), QStringLiteral("Bearer")},
								{QStringLiteral("expiresIn"), tokenLifetimeSeconds},
								{QStringLiteral("admin"), admin},
							},
							request.requestId);
		}

		/**
		 * @brief 返回认证主体类型与角色；用户和管理员附带标识状态，服务身份附带服务名。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse currentIdentity(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAuthentication, nullptr) << "Handling currentIdentity" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = authenticate(request, dependencies, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			QJsonObject identity{
				{QStringLiteral("principalType"), principal->type},
				{QStringLiteral("role"), principal->role},
			};
			if (principal->type == QStringLiteral("service")) {
				identity.insert(QStringLiteral("serviceName"), principal->serviceName);
			} else {
				identity.insert(QStringLiteral("principalId"), principal->id);
				identity.insert(QStringLiteral("status"), principal->status);
			}
			return jsonData(identity, request.requestId);
		}

		/**
		 * @brief 撤销当前访问令牌而不影响其他登录会话；拒绝服务身份注销。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse logout(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAuthentication, nullptr) << "Handling logout" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = authenticate(request, dependencies, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			if (principal->type == QStringLiteral("service")) {
				return jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("SERVICE 身份不支持退出"), {}, request.requestId, 403);
			}
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery revoke(database);
				revoke.prepare(QStringLiteral("UPDATE access_tokens SET revoked_at = ? WHERE token_hash = ? AND revoked_at IS NULL"));
				revoke.addBindValue(toDatabaseTimestamp(dependencies.clock->nowUtc()));
				revoke.addBindValue(Security::tokenHash(principal->token));
				if (revoke.exec()) {
					return true;
				}
				*operationError = revoke.lastError().text();
				return false;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			EV_LOG_INFO(backendAuthentication, nullptr) << "Logout succeeded" << "requestId=" << request.requestId << "principalType=" << principal->type << "principalId=" << principal->id;
			return HttpResponse{204, {}, {}, {}};
		}

	} // namespace

	/**
	 * @brief 注册认证路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAuthRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/user/login"), [dependencies](const HttpRequest &request) {
			return userLogin(request, dependencies);
		});
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/admin/login"), [dependencies](const HttpRequest &request) {
			return adminLogin(request, dependencies);
		});
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/auth/me"), [dependencies](const HttpRequest &request) {
			return currentIdentity(request, dependencies);
		});
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/auth/logout"), [dependencies](const HttpRequest &request) {
			return logout(request, dependencies);
		});
	}

} // namespace Backend
