/**
 * @file resource_support.h
 * @brief 到期预约清理及站点、充电桩的统一 JSON 投影。
 */

#ifndef BACKEND_RESOURCE_SUPPORT_H
#define BACKEND_RESOURCE_SUPPORT_H

#include <QDateTime>
#include <QJsonObject>

class QSqlDatabase;

namespace Backend {

	/**
	 * @brief 释放到期活动预约对应的 reserved 桩，并将这些预约标为 expired；事务由调用方负责。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 释放桩和更新到期预约两个 SQL 均成功返回 true；任一步失败返回 false 并写入错误。
	 * @note 调用方应置于事务内，以保证释放桩与变更预约状态的原子性。
	 */
	bool expireDueReservations(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage);
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
	bool loadStationJson(QSqlDatabase &database, qint64 stationId, bool includeInactive, bool includeDeleted, QJsonObject *station, bool *found, QString *errorMessage);
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
	bool loadChargerJson(QSqlDatabase &database, qint64 chargerId, bool includeDeleted, QJsonObject *charger, bool *found, QString *errorMessage);

} // namespace Backend

#endif
