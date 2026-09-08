/**
 * @file order_support.h
 * @brief 订单统一投影，充电中订单使用注入时间计算即时计量。
 */

#ifndef BACKEND_ORDER_SUPPORT_H
#define BACKEND_ORDER_SUPPORT_H

#include <QDateTime>
#include <QJsonObject>

#include <optional>

class QSqlDatabase;

namespace Backend {

	/**
	 * @brief 读取订单；充电中使用 nowUtc 即时计算，停止后的订单使用已持久化计量。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param orderId 订单标识。
	 * @param userId 用户标识；可选类型为空时不限制订单归属。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] order 只有返回 true 且 found 为 true 时才可读取的资源 JSON；指针必须有效。
	 * @param[out] found SQL 成功时写入记录是否存在；true 返回值并不保证找到记录。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return SQL/计量执行成功返回 true（可能 found 为 false）；执行失败返回 false。
	 * @note found 与结果对象指针必须有效；仅在 found 为 true 时读取投影。
	 */
	bool loadOrderJson(QSqlDatabase &database, qint64 orderId, std::optional<qint64> userId, const QDateTime &nowUtc, QJsonObject *order, bool *found, QString *errorMessage);

} // namespace Backend

#endif
