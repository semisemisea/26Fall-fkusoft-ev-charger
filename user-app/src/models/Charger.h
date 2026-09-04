#pragma once

#include <QString>

class QJsonObject;

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

    static Charger fromJson(const QJsonObject &object);
    static QString typeLabel(const Charger &charger);
    static QString statusLabel(const QString &status);
    static QString statusColor(const QString &status);
    static QString statusBgColor(const QString &status);
};
