#pragma once

#include <QString>

class QJsonObject;

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

    static Station fromJson(const QJsonObject &object);
};
