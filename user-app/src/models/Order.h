#pragma once

#include <QString>
#include <QtGlobal>

class QJsonObject;

struct Order
{
    int id = 0;
    QString orderNo;
    int userId = 0;
    int stationId = 0;
    int chargerId = 0;
    QString status;
    QString startedAt;
    QString endedAt;
    double energyKwh = 0;
    int durationMinutes = 0;
    qlonglong unitPriceFenPerKwh = 0;
    qlonglong amountFen = 0;
    QString stationName;
    QString chargerCode;

    static Order fromJson(const QJsonObject &object);
    static QString statusLabel(const QString &status);
};
