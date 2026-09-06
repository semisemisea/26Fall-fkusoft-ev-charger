#pragma once

#include <QString>

class QJsonObject;

// 电站：位置、单价与电桩统计（距离由服务端球面计算，客户端不自行推断）
struct Station
{
    int id = 0;
    QString name;
    QString address;
    double latitude = 0;
    double longitude = 0;
    qlonglong pricePerKwhFen = 0;
    int chargerCount = 0;
    int availableChargerCount = 0;
    double onlineRate = 0;
    double distanceKm = 0;
    QString status;

    // 从服务端 JSON 对象构造 Station
    static Station fromJson(const QJsonObject &object);
};
