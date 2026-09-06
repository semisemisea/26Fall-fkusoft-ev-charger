#ifndef BACKEND_API_SUPPORT_H
#define BACKEND_API_SUPPORT_H

#include "backend/api.h"
#include "backend/http.h"

#include <QJsonObject>

#include <optional>

class QSqlDatabase;
class QDateTime;

namespace Backend {

	struct Principal {
		QString type;
		qint64 id = 0;
		QString role;
		QString status;
		QString serviceName;
		QString token;
	};

	enum class IdempotencyState {
		Missing,
		Replay,
		Reused,
	};

	struct IdempotencyResult {
		IdempotencyState state = IdempotencyState::Missing;
		int status = 0;
		QJsonObject data;
	};

	std::optional<QJsonObject> parseJsonObject(const HttpRequest &request, HttpResponse *failure);
	std::optional<Principal> authenticate(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure);
	QJsonObject userJson(qint64 id, const QString &phone, const QString &nickname, bool hasAvatar, qint64 balanceFen, const QString &status, const QString &createdAt);
	HttpResponse databaseFailure(const QString &requestId);
	bool beginTransaction(QSqlDatabase &database);
	bool commitTransaction(QSqlDatabase &database);
	bool checkIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, IdempotencyResult *result, QString *errorMessage);
	bool storeIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, int retentionHours, int status, const QJsonObject &data, QString *errorMessage);

	void registerHealthRoutes(Router &router, const ApiDependencies &dependencies);
	void registerAuthRoutes(Router &router, const ApiDependencies &dependencies);
	void registerLocationRoutes(Router &router, const ApiDependencies &dependencies);
	void registerAdminRoutes(Router &router, const ApiDependencies &dependencies);
	void registerAdminUserRoutes(Router &router, const ApiDependencies &dependencies);
	void registerUserRoutes(Router &router, const ApiDependencies &dependencies);
	void registerStationRoutes(Router &router, const ApiDependencies &dependencies);
	void registerChargerRoutes(Router &router, const ApiDependencies &dependencies);
	void registerReservationRoutes(Router &router, const ApiDependencies &dependencies);
	void registerOrderRoutes(Router &router, const ApiDependencies &dependencies);

} // namespace Backend

#endif
