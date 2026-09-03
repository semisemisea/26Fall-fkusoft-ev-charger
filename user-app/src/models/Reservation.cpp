#include "Reservation.h"

#include <QJsonObject>

Reservation Reservation::fromJson(const QJsonObject &object)
{
    Reservation reservation;
    reservation.id = object.value(QLatin1String("id")).toInt();
    reservation.chargerId = object.value(QLatin1String("chargerId")).toInt();
    reservation.stationId = object.value(QLatin1String("stationId")).toInt();
    reservation.userId = object.value(QLatin1String("userId")).toInt();
    reservation.status = object.value(QLatin1String("status")).toString();
    reservation.startAt = object.value(QLatin1String("startAt")).toString();
    reservation.expiresAt =
        QDateTime::fromString(object.value(QLatin1String("expiresAt")).toString(), Qt::ISODate);
    reservation.stationName = object.value(QLatin1String("stationName")).toString();
    reservation.chargerCode = object.value(QLatin1String("chargerCode")).toString();
    return reservation;
}

QString Reservation::statusLabel(const QString &status)
{
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
