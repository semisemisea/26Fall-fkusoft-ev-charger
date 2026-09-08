/**
 * @file Reservation.cpp
 * @brief 解析预约及服务端到期时间，供预约记录和倒计时界面使用。
 */
#include "Reservation.h"

#include <QJsonObject>

/**
 * @details 逐字段解析 JSON；expiresAt 为 ISO 格式时间串，转为 QDateTime 便于倒计时
 */
Reservation Reservation::fromJson(const QJsonObject &object) {
	Reservation reservation;
	reservation.id = object.value(QLatin1String("id")).toInt();
	reservation.chargerId = object.value(QLatin1String("chargerId")).toInt();
	reservation.stationId = object.value(QLatin1String("stationId")).toInt();
	reservation.userId = object.value(QLatin1String("userId")).toInt();
	reservation.status = object.value(QLatin1String("status")).toString();
	reservation.startAt = object.value(QLatin1String("createdAt")).toString();
	reservation.expiresAt =
		QDateTime::fromString(object.value(QLatin1String("expiresAt")).toString(), Qt::ISODate);
	reservation.stationName = QStringLiteral("电站 %1").arg(reservation.stationId);
	reservation.chargerCode = QString::number(reservation.chargerId);
	return reservation;
}

/**
 * @details 状态枚举到中文文案的映射：active/used/cancelled/expired
 */
QString Reservation::statusLabel(const QString &status) {
	if (status == QLatin1String("active")) {
		return QStringLiteral("预约中");
	}
	if (status == QLatin1String("used")) {
		return QStringLiteral("已使用");
	}
	if (status == QLatin1String("cancelled")) {
		return QStringLiteral("已取消");
	}
	if (status == QLatin1String("expired")) {
		return QStringLiteral("已过期");
	}
	return QStringLiteral("未知状态");
}
