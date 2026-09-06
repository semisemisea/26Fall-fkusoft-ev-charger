#include "resource_support.h"

#include "backend/database.h"

#include <QSqlError>
#include <QSqlQuery>

namespace Backend {

	bool expireDueReservations(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) {
		const QString now = toDatabaseTimestamp(nowUtc);
		QSqlQuery release(database);
		release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status = 'available', updated_at = ? WHERE occupancy_status = 'reserved' AND id IN (SELECT charger_id FROM reservations WHERE status = 'active' AND expires_at <= ?)"));
		release.addBindValue(now);
		release.addBindValue(now);
		if (!release.exec()) {
			*errorMessage = release.lastError().text();
			return false;
		}
		QSqlQuery expire(database);
		expire.prepare(QStringLiteral("UPDATE reservations SET status = 'expired', updated_at = ? WHERE status = 'active' AND expires_at <= ?"));
		expire.addBindValue(now);
		expire.addBindValue(now);
		if (!expire.exec()) {
			*errorMessage = expire.lastError().text();
			return false;
		}
		return true;
	}

	bool loadStationJson(QSqlDatabase &database, qint64 stationId, bool includeInactive, bool includeDeleted, QJsonObject *station, bool *found, QString *errorMessage) {
		QSqlQuery query(database);
		QString sql = QStringLiteral("SELECT id,name,address,latitude,longitude,price_fen_per_kwh,status,created_at,updated_at,deleted_at FROM stations WHERE id = ?");
		if (!includeInactive) {
			sql += QStringLiteral(" AND status = 'active'");
		}
		if (!includeDeleted) {
			sql += QStringLiteral(" AND deleted_at IS NULL");
		}
		query.prepare(sql);
		query.addBindValue(stationId);
		if (!query.exec()) {
			*errorMessage = query.lastError().text();
			return false;
		}
		if (!query.next()) {
			*found = false;
			return true;
		}
		QSqlQuery counts(database);
		counts.prepare(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(CASE WHEN occupancy_status = 'available' AND operational_status = 'online' THEN 1 ELSE 0 END),0) FROM chargers WHERE station_id = ? AND deleted_at IS NULL"));
		counts.addBindValue(stationId);
		if (!counts.exec() || !counts.next()) {
			*errorMessage = counts.lastError().text();
			return false;
		}
		*station = QJsonObject{
			{QStringLiteral("id"), query.value(0).toLongLong()},
			{QStringLiteral("name"), query.value(1).toString()},
			{QStringLiteral("address"), query.value(2).toString()},
			{QStringLiteral("latitude"), query.value(3).toDouble()},
			{QStringLiteral("longitude"), query.value(4).toDouble()},
			{QStringLiteral("priceFenPerKwh"), query.value(5).toLongLong()},
			{QStringLiteral("status"), query.value(6).toString()},
			{QStringLiteral("chargerCount"), counts.value(0).toLongLong()},
			{QStringLiteral("availableChargerCount"), counts.value(1).toLongLong()},
			{QStringLiteral("createdAt"), query.value(7).toString()},
			{QStringLiteral("updatedAt"), query.value(8).toString()},
			{QStringLiteral("deletedAt"), query.isNull(9) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(9).toString())},
		};
		*found = true;
		return true;
	}

} // namespace Backend
