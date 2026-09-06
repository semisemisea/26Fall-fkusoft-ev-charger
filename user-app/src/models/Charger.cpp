#include "Charger.h"

#include "common/Theme.h"

#include <QJsonObject>

// 逐字段解析 JSON
Charger Charger::fromJson(const QJsonObject &object)
{
    Charger charger;
    charger.id = object.value(QLatin1String("id")).toInt();
    charger.stationId = object.value(QLatin1String("stationId")).toInt();
	charger.code = QString::number(charger.id);
	charger.type = object.value(QLatin1String("type")).toString();
    charger.powerKw = object.value(QLatin1String("powerKw")).toDouble();
	const QString operationalStatus = object.value(QLatin1String("operationalStatus")).toString();
	charger.status = operationalStatus == QLatin1String("online")
						 ? object.value(QLatin1String("occupancyStatus")).toString()
						 : operationalStatus;
	charger.totalChargeCount = object.value(QLatin1String("totalChargeCount")).toInt();
    charger.totalChargeMinutes = object.value(QLatin1String("totalChargeMinutes")).toInt();
    return charger;
}

// fast -> 快充，slow -> 慢充
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

// 状态枚举到中文文案的映射
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

// 徽章文字色：available 绿、reserved/charging 黄、fault 红、offline 灰
QString Charger::statusColor(const QString &status)
{
    if (status == QLatin1String("available")) {
        return theme::successInkName();
    }
    if (status == QLatin1String("reserved")) {
        return QStringLiteral("#713F12");
    }
    if (status == QLatin1String("charging")) {
        return QStringLiteral("#713F12");
    }
    if (status == QLatin1String("fault")) {
        return theme::errorDeep().name();
    }
    if (status == QLatin1String("offline")) {
        return theme::neutralGray().name();
    }
    return theme::textSecondaryName();
}

// 徽章背景色，与 statusColor 配色一一对应
QString Charger::statusBgColor(const QString &status)
{
    if (status == QLatin1String("available")) {
        return theme::successBg().name();
    }
    if (status == QLatin1String("reserved")) {
        return QStringLiteral("#FEF08A");
    }
    if (status == QLatin1String("charging")) {
        return QStringLiteral("#FEF08A");
    }
    if (status == QLatin1String("fault")) {
        return theme::errorBg().name();
    }
    if (status == QLatin1String("offline")) {
        return theme::fillHover().name();
    }
    return theme::split().name();
}
