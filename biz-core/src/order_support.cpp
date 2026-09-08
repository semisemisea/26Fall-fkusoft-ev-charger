/**
 * @file order_support.cpp
 * @brief 订单统一投影，充电中订单使用注入时间计算即时计量。
 */

#include "order_support.h"

#include "evcharger/charging.h"

#include <QSqlError>
#include <QSqlQuery>

#include <limits>

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
	bool loadOrderJson(QSqlDatabase &database, qint64 orderId, std::optional<qint64> userId, const QDateTime &nowUtc, QJsonObject *order, bool *found, QString *errorMessage) {
		QSqlQuery query(database);
		QString sql = QStringLiteral("SELECT id,user_id,station_id,charger_id,reservation_id,status,power_w,unit_price_fen_per_kwh,started_at,stopped_at,settled_at,duration_seconds,energy_ws,amount_fen,created_at,updated_at FROM orders WHERE id=?");
		if (userId.has_value()) {
			sql += QStringLiteral(" AND user_id=?");
		}
		query.prepare(sql);
		query.addBindValue(orderId);
		if (userId.has_value()) {
			query.addBindValue(*userId);
		}
		if (!query.exec()) {
			*errorMessage = query.lastError().text();
			return false;
		}
		if (!query.next()) {
			*found = false;
			return true;
		}
		qint64 duration = query.value(11).toLongLong();
		qint64 energyWs = query.value(12).toLongLong();
		qint64 amount = query.value(13).toLongLong();
		qint64 energyHundredths = 0;
		// 充电中仅计算展示快照；停止操作才持久化最终计量，避免读取产生写入。
		if (query.value(5).toString() == QStringLiteral("charging")) {
			const auto metrics = EvCharger::calculateCharge(query.value(6).toLongLong(), query.value(7).toLongLong(), QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs), nowUtc);
			if (!metrics.has_value()) {
				*errorMessage = QStringLiteral("Unable to calculate charging order");
				return false;
			}
			duration = metrics->durationSeconds;
			energyWs = metrics->energyWs;
			amount = metrics->amountFen;
			energyHundredths = metrics->energyHundredthsKwh;
		} else {
			if (energyWs > 0 && energyWs > (std::numeric_limits<qint64>::max() - 1'800'000) / 100) {
				*errorMessage = QStringLiteral("Unable to format order energy");
				return false;
			}
			energyHundredths = (energyWs * 100 + 1'800'000) / 3'600'000;
		}
		*order = QJsonObject{
			{QStringLiteral("id"), query.value(0).toLongLong()},
			{QStringLiteral("userId"), query.value(1).toLongLong()},
			{QStringLiteral("stationId"), query.value(2).toLongLong()},
			{QStringLiteral("chargerId"), query.value(3).toLongLong()},
			{QStringLiteral("reservationId"), query.isNull(4) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(4).toLongLong())},
			{QStringLiteral("status"), query.value(5).toString()},
			{QStringLiteral("powerKw"), query.value(6).toLongLong() / 1000.0},
			{QStringLiteral("unitPriceFenPerKwh"), query.value(7).toLongLong()},
			{QStringLiteral("startedAt"), query.value(8).toString()},
			{QStringLiteral("stoppedAt"), query.isNull(9) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(9).toString())},
			{QStringLiteral("settledAt"), query.isNull(10) ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(10).toString())},
			{QStringLiteral("durationSeconds"), duration},
			{QStringLiteral("durationMinutes"), duration / 60},
			{QStringLiteral("energyKwh"), energyHundredths / 100.0},
			{QStringLiteral("amountFen"), amount},
			{QStringLiteral("createdAt"), query.value(14).toString()},
			{QStringLiteral("updatedAt"), query.value(15).toString()},
		};
		*found = true;
		return true;
	}

} // namespace Backend
