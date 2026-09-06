#include "api_support.h"

#include "backend/database.h"
#include "backend/security.h"
#include "evcharger/clock.h"

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
			*failure = databaseFailure(request.requestId);
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

	HttpResponse databaseFailure(const QString &requestId) {
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
