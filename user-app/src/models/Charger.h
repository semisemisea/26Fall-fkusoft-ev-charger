#pragma once

#include <QString>

class QJsonObject;

// 电桩：状态枚举 available/reserved/charging/fault/offline，见 docs/apis.md
struct Charger
{
    int id = 0;
    int stationId = 0;
    QString code;
    QString type;
    double powerKw = 0;
    QString status;
    int totalChargeCount = 0;
    int totalChargeMinutes = 0;

    // 从服务端 JSON 对象构造 Charger
    static Charger fromJson(const QJsonObject &object);
    // 类型（fast/slow）的中文标签
    static QString typeLabel(const Charger &charger);
    // 状态的中文标签
    static QString statusLabel(const QString &status);
    // 状态徽章文字颜色（十六进制色值）
    static QString statusColor(const QString &status);
    // 状态徽章背景颜色（十六进制色值）
    static QString statusBgColor(const QString &status);
};
