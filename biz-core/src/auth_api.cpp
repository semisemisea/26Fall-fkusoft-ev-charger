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

		constexpr qint64 tokenLifetimeSeconds = 7 * 24 * 60 * 60;

		HttpResponse validationError(const HttpRequest &request, const QString &field, const QString &reason) {
			return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("请求参数无效"), QJsonObject{{field, reason}}, request.requestId, 400);
		}

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
