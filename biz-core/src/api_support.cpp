/**
 * @file api_support.cpp
 * @brief 身份验证、JSON 响应、事务与幂等记录的共享 API 支持。
 */

#include "api_support.h"

#include "backend/database.h"
#include "backend/security.h"
#include "evcharger/clock.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace Backend {

	/**
	 * @brief 检查 JSON 内容类型并解析对象请求体；类型或语法不符时生成 HTTP 错误。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
	 * @return 解析出的 JSON 对象；内容类型不受支持、JSON 语法错误或根值不是对象时返回 std::nullopt，并写入 failure。
	 */
	std::optional<QJsonObject> parseJsonObject(const HttpRequest &request, HttpResponse *failure) {
		const QByteArray contentType = request.headers.value(QByteArrayLiteral("content-type")).toLower();
		if (contentType.split(';').first().trimmed() != QByteArrayLiteral("application/json")) {
			*failure = jsonError(QStringLiteral("UNSUPPORTED_MEDIA_TYPE"), QStringLiteral("Content-Type 必须是 application/json"), {}, request.requestId, 415);
			return std::nullopt;
		}
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(request.body, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("JSON 正文必须是对象"), {}, request.requestId, 400);
			return std::nullopt;
		}
		return document.object();
	}

	/**
	 * @brief 验证 Bearer 令牌摘要、有效期与撤销状态，并加载用户、管理员或服务身份。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
	 * @return 已加载的用户、管理员或服务主体；令牌无效/过期/已撤销、主体不存在或数据库失败时返回 std::nullopt，并写入 failure。
	 */
	std::optional<Principal> authenticate(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure) {
		const QByteArray authorization = request.headers.value(QByteArrayLiteral("authorization"));
		if (!authorization.startsWith(QByteArrayLiteral("Bearer ")) || authorization.size() <= 7) {
			*failure = jsonError(QStringLiteral("UNAUTHORIZED"), QStringLiteral("需要有效的访问令牌"), {}, request.requestId, 401);
			return std::nullopt;
		}
		const QString token = QString::fromUtf8(authorization.mid(7));
		const QByteArray hash = Security::tokenHash(token);
		const QString now = toDatabaseTimestamp(dependencies.clock->nowUtc());
		Principal principal;
		bool found = false;
		QString databaseError;
		const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
			QSqlQuery service(database);
			service.prepare(QStringLiteral("SELECT service_name, role FROM service_credentials WHERE token_hash = ?"));
			service.addBindValue(hash);
			if (!service.exec()) {
				*operationError = service.lastError().text();
				return false;
			}
			if (service.next()) {
				principal.type = QStringLiteral("service");
				principal.role = service.value(1).toString();
				principal.serviceName = service.value(0).toString();
				principal.token = token;
				found = true;
				return true;
			}

			QSqlQuery access(database);
			access.prepare(QStringLiteral("SELECT principal_type, principal_id, role FROM access_tokens WHERE token_hash = ? AND revoked_at IS NULL AND expires_at > ?"));
			access.addBindValue(hash);
			access.addBindValue(now);
			if (!access.exec()) {
				*operationError = access.lastError().text();
				return false;
			}
			if (!access.next()) {
				return true;
			}
			principal.type = access.value(0).toString();
			principal.id = access.value(1).toLongLong();
			principal.role = access.value(2).toString();
			principal.token = token;
			QSqlQuery subject(database);
			if (principal.type == QStringLiteral("user")) {
				subject.prepare(QStringLiteral("SELECT status FROM users WHERE id = ?"));
			} else {
				subject.prepare(QStringLiteral("SELECT status FROM admins WHERE id = ?"));
			}
			subject.addBindValue(principal.id);
			if (!subject.exec()) {
				*operationError = subject.lastError().text();
				return false;
			}
			if (subject.next()) {
				principal.status = subject.value(0).toString();
				found = true;
			}
			return true;
		},
																   &databaseError);
		if (!success) {
			*failure = databaseFailure(request.requestId, databaseError);
			return std::nullopt;
		}
		if (!found) {
			*failure = jsonError(QStringLiteral("UNAUTHORIZED"), QStringLiteral("需要有效的访问令牌"), {}, request.requestId, 401);
			return std::nullopt;
		}
		return principal;
	}

	/**
	 * @brief 将用户字段投影为 API JSON；头像以是否存在表示，不包含头像二进制。
	 * @param id 用户数据库标识。
	 * @param phone 由 11 个 ASCII 数字组成的手机号文本。
	 * @param nickname 用户昵称。
	 * @param hasAvatar 用户是否已保存头像。
	 * @param balanceFen 钱包余额，单位分。
	 * @param status 用户或资源的业务状态。
	 * @param createdAt 资源创建时间的数据库文本。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject userJson(qint64 id, const QString &phone, const QString &nickname, bool hasAvatar, qint64 balanceFen, const QString &status, const QString &createdAt) {
		return QJsonObject{
			{QStringLiteral("id"), id},
			{QStringLiteral("phone"), phone},
			{QStringLiteral("nickname"), nickname},
			{QStringLiteral("hasAvatar"), hasAvatar},
			{QStringLiteral("walletBalanceFen"), balanceFen},
			{QStringLiteral("status"), status},
			{QStringLiteral("createdAt"), createdAt},
		};
	}

	/**
	 * @brief 将 SQLite 忙或锁错误映射为 503，其余数据库错误映射为通用 500。
	 * @param requestId 本次请求关联标识。
	 * @param[in] errorMessage 已发生的数据库错误文本，用于区分锁忙与内部错误。
	 * @return HTTP 503 数据库忙响应或 HTTP 500 内部错误响应。
	 */
	HttpResponse databaseFailure(const QString &requestId, const QString &errorMessage) {
		const QString normalized = errorMessage.toLower();
		if (normalized.contains(QStringLiteral("database is locked")) || normalized.contains(QStringLiteral("database is busy")) || normalized.contains(QStringLiteral("database table is locked"))) {
			return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("数据库暂时繁忙"), {}, requestId, 503);
		}
		return jsonError(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("服务内部错误"), {}, requestId, 500);
	}

	/**
	 * @brief 通过 Qt SQL 驱动开始事务，将后续关联业务写入纳入同一提交范围。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @return 驱动成功开始事务返回 true，无法开始事务返回 false；错误可从连接读取。
	 */
	bool beginTransaction(QSqlDatabase &database) {
		return database.transaction();
	}

	/**
	 * @brief 提交事务，提交失败时回滚以防留下未结束写事务。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @return 提交成功返回 true；提交失败返回 false，并尝试回滚。
	 */
	bool commitTransaction(QSqlDatabase &database) {
		if (database.commit()) {
			return true;
		}
		database.rollback();
		return false;
	}

	namespace {

		/**
		 * @brief 对规范化对象的紧凑 JSON 计算 SHA-256，用于比较幂等请求内容。
		 * @param body 待解析或序列化的 JSON 请求对象。
		 * @return 按上述规则生成的文本或字节结果。
		 */
		QByteArray normalizedHash(const QJsonObject &body) {
			return QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
		}

	} // namespace

	/**
	 * @brief 清理过期记录后按身份、方法、路径和幂等键查询，区分首次请求、可重放及请求内容冲突。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param principal 已认证主体，用于权限及幂等记录隔离。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param normalizedBody 去除无关差异后的请求对象，用于计算幂等摘要。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] result SQL 成功时写入 Missing、Replay 或 Reused；Replay 同时含保存的状态码和响应数据。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 清理和查询均成功返回 true，此时 result 指明 Missing、Replay 或 Reused；SQL 失败返回 false。
	 */
	bool checkIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, IdempotencyResult *result, QString *errorMessage) {
		QSqlQuery cleanup(database);
		cleanup.prepare(QStringLiteral("DELETE FROM idempotency_records WHERE expires_at <= ?"));
		cleanup.addBindValue(toDatabaseTimestamp(nowUtc));
		if (!cleanup.exec()) {
			*errorMessage = cleanup.lastError().text();
			return false;
		}
		QSqlQuery existing(database);
		existing.prepare(QStringLiteral("SELECT request_hash,http_status,response_data FROM idempotency_records WHERE principal_type=? AND principal_id=? AND method=? AND path=? AND idempotency_key=?"));
		existing.addBindValue(principal.type);
		existing.addBindValue(principal.id);
		existing.addBindValue(request.method);
		existing.addBindValue(request.path);
		existing.addBindValue(QString::fromUtf8(request.headers.value(QByteArrayLiteral("idempotency-key"))));
		if (!existing.exec()) {
			*errorMessage = existing.lastError().text();
			return false;
		}
		if (!existing.next()) {
			result->state = IdempotencyState::Missing;
			return true;
		}
		if (existing.value(0).toByteArray() != normalizedHash(normalizedBody)) {
			result->state = IdempotencyState::Reused;
			return true;
		}
		result->state = IdempotencyState::Replay;
		result->status = existing.value(1).toInt();
		result->data = QJsonDocument::fromJson(existing.value(2).toByteArray()).object();
		return true;
	}

	/**
	 * @brief 保存请求摘要、响应数据、HTTP 状态和过期时间，与业务写入共享调用方事务。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param principal 已认证主体，用于权限及幂等记录隔离。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param normalizedBody 去除无关差异后的请求对象，用于计算幂等摘要。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param retentionHours 幂等结果保留小时数。
	 * @param status HTTP 响应状态码。
	 * @param data 返回给调用方或保存用于重放的数据。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 幂等记录插入成功返回 true，插入失败返回 false 并写入 SQL 错误。
	 */
	bool storeIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, int retentionHours, int status, const QJsonObject &data, QString *errorMessage) {
		QSqlQuery remember(database);
		remember.prepare(QStringLiteral("INSERT INTO idempotency_records(principal_type,principal_id,method,path,idempotency_key,request_hash,http_status,response_data,created_at,expires_at) VALUES (?,?,?,?,?,?,?,?,?,?)"));
		remember.addBindValue(principal.type);
		remember.addBindValue(principal.id);
		remember.addBindValue(request.method);
		remember.addBindValue(request.path);
		remember.addBindValue(QString::fromUtf8(request.headers.value(QByteArrayLiteral("idempotency-key"))));
		remember.addBindValue(normalizedHash(normalizedBody));
		remember.addBindValue(status);
		remember.addBindValue(QJsonDocument(data).toJson(QJsonDocument::Compact));
		remember.addBindValue(toDatabaseTimestamp(nowUtc));
		remember.addBindValue(toDatabaseTimestamp(nowUtc.addSecs(static_cast<qint64>(retentionHours) * 3600)));
		if (remember.exec()) {
			return true;
		}
		*errorMessage = remember.lastError().text();
		return false;
	}

	/**
	 * @brief 将业务错误包装为可保存的幂等数据，以便失败响应也能一致重放。
	 * @param code API 错误代码。
	 * @param message 面向调用方的错误说明。
	 * @param details 字段级校验详情。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject idempotencyError(const QString &code, const QString &message, const QJsonObject &details) {
		return QJsonObject{
			{QStringLiteral("_idempotencyError"), QJsonObject{
													  {QStringLiteral("code"), code},
													  {QStringLiteral("message"), message},
													  {QStringLiteral("details"), details},
												  }},
		};
	}

	/**
	 * @brief 用当前请求 ID 重建已保存响应；识别内部错误包装并恢复为错误信封。
	 * @param[in] result checkIdempotency 已加载的可重放记录，包含原始状态码和响应数据。
	 * @param requestId 本次请求关联标识。
	 * @return 保存的业务状态码与数据响应，或由内部错误包装恢复的错误响应；meta 使用当前请求 ID。
	 */
	HttpResponse replayIdempotency(const IdempotencyResult &result, const QString &requestId) {
		const QJsonValue storedError = result.data.value(QStringLiteral("_idempotencyError"));
		if (storedError.isObject()) {
			const QJsonObject stored = storedError.toObject();
			return jsonError(stored.value(QStringLiteral("code")).toString(), stored.value(QStringLiteral("message")).toString(), stored.value(QStringLiteral("details")).toObject(), requestId, result.status);
		}
		return jsonData(result.data, requestId, result.status);
	}

	/**
	 * @brief 注册健康检查路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerHealthRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/health"), [database = dependencies.database](const HttpRequest &request) {
			QString databaseError;
			const bool healthy = database->withConnection([](QSqlDatabase &connection, QString *operationError) {
				QSqlQuery query(connection);
				if (query.exec(QStringLiteral("SELECT 1")) && query.next()) {
					return true;
				}
				*operationError = query.lastError().text();
				return false;
			},
														  &databaseError);
			return healthy
					   ? jsonData(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}}, request.requestId)
					   : jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("服务暂不可用"), {}, request.requestId, 503);
		});
	}

} // namespace Backend
