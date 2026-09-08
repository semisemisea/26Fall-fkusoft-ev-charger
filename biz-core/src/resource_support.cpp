/**
 * @file resource_support.cpp
 * @brief 到期预约清理及站点、充电桩的统一 JSON 投影。
 */

#include "resource_support.h"

#include "backend/database.h"

#include <QSqlError>
#include <QSqlQuery>

namespace Backend {

	/**
	 * @brief 释放到期活动预约对应的 reserved 桩，并将这些预约标为 expired；事务由调用方负责。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 释放桩和更新到期预约两个 SQL 均成功返回 true；任一步失败返回 false 并写入错误。
	 * @note 调用方应置于事务内，以保证释放桩与变更预约状态的原子性。
	 */
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

	/**
	 * @brief 读取站点及有效桩数量，按开关决定是否包括停用和软删除站点。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param stationId 站点标识；可选类型为空时不按站点过滤。
	 * @param includeInactive 是否允许返回停用站点。
	 * @param includeDeleted 是否允许返回已软删除资源。
	 * @param[out] station 只有返回 true 且 found 为 true 时才可读取的资源 JSON；指针必须有效。
	 * @param[out] found SQL 成功时写入记录是否存在；true 返回值并不保证找到记录。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return SQL/计量执行成功返回 true（可能 found 为 false）；执行失败返回 false。
	 * @note found 与结果对象指针必须有效；仅在 found 为 true 时读取投影。
	 */
	bool loadStationJson(QSqlDatabase &database, qint64 stationId, bool includeInactive, bool includeDeleted, QJsonObject *station, bool *found, QString *errorMessage) {
		QSqlQuery query(database);
		QString sql = QStringLiteral("SELECT id,name,latitude,longitude,price_fen_per_kwh,status,created_at,updated_at,deleted_at FROM stations WHERE id = ?");
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
			{QStringLiteral("latitude"), query.value(2).toDouble()},
			{QStringLiteral("longitude"), query.value(3).toDouble()},
			{QStringLiteral("priceFenPerKwh"), query.value(4).toLongLong()},
			{QStringLiteral("status"), query.value(5).toString()},
			{QStringLiteral("chargerCount"), counts.value(0).toLongLong()},
			{QStringLiteral("availableChargerCount"), counts.value(1).toLongLong()},
			{QStringLiteral("createdAt"), query.value(6).toString()},
			{QStringLiteral("updatedAt"), query.value(7).toString()},
			{QStringLiteral("deletedAt"), query.isNull(8) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(8).toString())},
		};
		*found = true;
		return true;
	}

	/**
	 * @brief 读取充电桩统一投影，将整数瓦转换为千瓦并输出累计使用指标。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param chargerId 充电桩标识。
	 * @param includeDeleted 是否允许返回已软删除资源。
	 * @param[out] charger 只有返回 true 且 found 为 true 时才可读取的资源 JSON；指针必须有效。
	 * @param[out] found SQL 成功时写入记录是否存在；true 返回值并不保证找到记录。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return SQL/计量执行成功返回 true（可能 found 为 false）；执行失败返回 false。
	 * @note found 与结果对象指针必须有效；仅在 found 为 true 时读取投影。
	 */
	bool loadChargerJson(QSqlDatabase &database, qint64 chargerId, bool includeDeleted, QJsonObject *charger, bool *found, QString *errorMessage) {
		QSqlQuery query(database);
		QString sql = QStringLiteral("SELECT id,station_id,type,power_w,occupancy_status,operational_status,total_charge_count,total_charge_seconds,created_at,updated_at,deleted_at FROM chargers WHERE id=?");
		if (!includeDeleted) {
			sql += QStringLiteral(" AND deleted_at IS NULL");
		}
		query.prepare(sql);
		query.addBindValue(chargerId);
		if (!query.exec()) {
			*errorMessage = query.lastError().text();
			return false;
		}
		if (!query.next()) {
			*found = false;
			return true;
		}
		const qint64 seconds = query.value(7).toLongLong();
		*charger = QJsonObject{
			{QStringLiteral("id"), query.value(0).toLongLong()},
			{QStringLiteral("stationId"), query.value(1).toLongLong()},
			{QStringLiteral("type"), query.value(2).toString()},
			{QStringLiteral("powerKw"), query.value(3).toLongLong() / 1000.0},
			{QStringLiteral("occupancyStatus"), query.value(4).toString()},
			{QStringLiteral("operationalStatus"), query.value(5).toString()},
			{QStringLiteral("totalChargeCount"), query.value(6).toLongLong()},
			{QStringLiteral("totalChargeSeconds"), seconds},
			{QStringLiteral("totalChargeMinutes"), seconds / 60},
			{QStringLiteral("createdAt"), query.value(8).toString()},
			{QStringLiteral("updatedAt"), query.value(9).toString()},
			{QStringLiteral("deletedAt"), query.isNull(10) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(10).toString())},
		};
		*found = true;
		return true;
	}

} // namespace Backend
