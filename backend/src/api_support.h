#ifndef BACKEND_API_SUPPORT_H
#define BACKEND_API_SUPPORT_H

#include "backend/api.h"
#include "backend/http.h"

#include <QJsonObject>

#include <optional>

class QSqlDatabase;

namespace Backend {

	struct Principal {
		QString type;
		qint64 id = 0;
		QString role;
		QString status;
		QString serviceName;
		QString token;
	};

	std::optional<QJsonObject> parseJsonObject(const HttpRequest &request, HttpResponse *failure);
	std::optional<Principal> authenticate(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure);
	QJsonObject userJson(qint64 id, const QString &phone, const QString &nickname, bool hasAvatar, qint64 balanceFen, const QString &status, const QString &createdAt);
	HttpResponse databaseFailure(const QString &requestId);
	bool beginTransaction(QSqlDatabase &database);
	bool commitTransaction(QSqlDatabase &database);

	void registerHealthRoutes(Router &router, const ApiDependencies &dependencies);
	void registerAuthRoutes(Router &router, const ApiDependencies &dependencies);
	void registerUserRoutes(Router &router, const ApiDependencies &dependencies);

} // namespace Backend

#endif
