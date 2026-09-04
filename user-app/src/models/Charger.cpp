#include "Charger.h"

#include "common/Theme.h"

#include <QJsonObject>

Charger Charger::fromJson(const QJsonObject &object)
{
    Charger charger;
    charger.id = object.value(QLatin1String("id")).toInt();
    charger.stationId = object.value(QLatin1String("stationId")).toInt();
    charger.code = object.value(QLatin1String("code")).toString();
    charger.type = object.value(QLatin1String("type")).toString();
    charger.powerKw = object.value(QLatin1String("powerKw")).toDouble();
    charger.status = object.value(QLatin1String("status")).toString();
    charger.totalChargeCount = object.value(QLatin1String("totalChargeCount")).toInt();
    charger.totalChargeMinutes = object.value(QLatin1String("totalChargeMinutes")).toInt();
    return charger;
}

QString Charger::typeLabel(const Charger &charger)
{
    if (charger.type == QLatin1String("fast")) {
        return QStringLiteral("快充");
    }
    if (charger.type == QLatin1String("slow")) {
        return QStringLiteral("慢充");
    }
    return QStringLiteral("未知类型");
}

QString Charger::statusLabel(const QString &status)
{
    if (status == QLatin1String("available")) {
        return QStringLiteral("可用");
    }
    if (status == QLatin1String("reserved")) {
        return QStringLiteral("已预约");
    }
    if (status == QLatin1String("charging")) {
        return QStringLiteral("充电中");
    }
    if (status == QLatin1String("fault")) {
        return QStringLiteral("故障");
    }
    if (status == QLatin1String("offline")) {
        return QStringLiteral("离线");
    }
    return QStringLiteral("未知状态");
}

QString Charger::statusColor(const QString &status)
{
    if (status == QLatin1String("available")) {
        return theme::successInkName();
    }
    if (status == QLatin1String("reserved")) {
        return theme::warningInkName();
    }
    if (status == QLatin1String("charging")) {
        return theme::primaryDeepName();
    }
    if (status == QLatin1String("fault")) {
        return theme::errorDeep().name();
    }
    if (status == QLatin1String("offline")) {
        return theme::neutralGray().name();
    }
    return theme::textSecondaryName();
}

QString Charger::statusBgColor(const QString &status)
{
    if (status == QLatin1String("available")) {
        return theme::successBg().name();
    }
    if (status == QLatin1String("reserved")) {
        return theme::warningBg().name();
    }
    if (status == QLatin1String("charging")) {
        return theme::primaryBg().name();
    }
    if (status == QLatin1String("fault")) {
        return theme::errorBg().name();
    }
    if (status == QLatin1String("offline")) {
        return theme::fillHover().name();
    }
    return theme::split().name();
}
