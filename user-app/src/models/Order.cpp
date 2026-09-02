#include "Order.h"

#include <QJsonObject>

Order Order::fromJson(const QJsonObject &object)
{
    Order order;
    order.id = object.value(QLatin1String("id")).toInt();
    order.orderNo = object.value(QLatin1String("orderNo")).toString();
    order.userId = object.value(QLatin1String("userId")).toInt();
    order.stationId = object.value(QLatin1String("stationId")).toInt();
    order.chargerId = object.value(QLatin1String("chargerId")).toInt();
    order.status = object.value(QLatin1String("status")).toString();
    order.startedAt = object.value(QLatin1String("startedAt")).toString();
    order.endedAt = object.value(QLatin1String("endedAt")).toString();
    order.energyKwh = object.value(QLatin1String("energyKwh")).toDouble();
    order.durationMinutes = object.value(QLatin1String("durationMinutes")).toInt();
    order.unitPriceFenPerKwh = object.value(QLatin1String("unitPriceFenPerKwh")).toInteger();
    order.amountFen = object.value(QLatin1String("amountFen")).toInteger();
    order.stationName = object.value(QLatin1String("stationName")).toString();
    order.chargerCode = object.value(QLatin1String("chargerCode")).toString();
    return order;
}

QString Order::statusLabel(const QString &status)
{
    if (status == QLatin1String("charging")) {
        return QStringLiteral("充电中");
    }
    if (status == QLatin1String("awaiting_payment")) {
        return QStringLiteral("待支付");
    }
    if (status == QLatin1String("settled")) {
        return QStringLiteral("已结算");
    }
    if (status == QLatin1String("cancelled")) {
        return QStringLiteral("已取消");
    }
    if (status == QLatin1String("failed")) {
        return QStringLiteral("已失败");
    }
    return QStringLiteral("未知状态");
}
