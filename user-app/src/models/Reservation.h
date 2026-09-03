#pragma once

#include <QDateTime>
#include <QString>

class QJsonObject;

struct Reservation
{
    int id = 0;
    int chargerId = 0;
    int stationId = 0;
    int userId = 0;
    QString status;
    QString startAt;
    QDateTime expiresAt;
    QString stationName;
    QString chargerCode;

    static Reservation fromJson(const QJsonObject &object);
    static QString statusLabel(const QString &status);
};
