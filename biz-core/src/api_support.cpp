#include "api_support.h"

#include "backend/database.h"
#include "backend/security.h"
#include "evcharger/clock.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace Backend {

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

	HttpResponse databaseFailure(const QString &requestId, const QString &errorMessage) {
		const QString normalized = errorMessage.toLower();
		if (normalized.contains(QStringLiteral("database is locked")) || normalized.contains(QStringLiteral("database is busy")) || normalized.contains(QStringLiteral("database table is locked"))) {
			return jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("数据库暂时繁忙"), {}, requestId, 503);
		}
		return jsonError(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("服务内部错误"), {}, requestId, 500);
	}

	bool beginTransaction(QSqlDatabase &database) {
		return database.transaction();
	}

	bool commitTransaction(QSqlDatabase &database) {
		if (database.commit()) {
			return true;
		}
		database.rollback();
		return false;
	}

	namespace {

		QByteArray normalizedHash(const QJsonObject &body) {
			return QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
		}

	} // namespace

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

	QJsonObject idempotencyError(const QString &code, const QString &message, const QJsonObject &details) {
		return QJsonObject{
			{QStringLiteral("_idempotencyError"), QJsonObject{
													  {QStringLiteral("code"), code},
													  {QStringLiteral("message"), message},
													  {QStringLiteral("details"), details},
												  }},
		};
	}

	HttpResponse replayIdempotency(const IdempotencyResult &result, const QString &requestId) {
		const QJsonValue storedError = result.data.value(QStringLiteral("_idempotencyError"));
		if (storedError.isObject()) {
			const QJsonObject stored = storedError.toObject();
			return jsonError(stored.value(QStringLiteral("code")).toString(), stored.value(QStringLiteral("message")).toString(), stored.value(QStringLiteral("details")).toObject(), requestId, result.status);
		}
		return jsonData(result.data, requestId, result.status);
	}

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
