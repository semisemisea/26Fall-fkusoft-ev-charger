#ifndef BACKEND_RESOURCE_SUPPORT_H
#define BACKEND_RESOURCE_SUPPORT_H

#include <QDateTime>
#include <QJsonObject>

class QSqlDatabase;

namespace Backend {

	bool expireDueReservations(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage);
	bool loadStationJson(QSqlDatabase &database, qint64 stationId, bool includeInactive, bool includeDeleted, QJsonObject *station, bool *found, QString *errorMessage);

} // namespace Backend

#endif
