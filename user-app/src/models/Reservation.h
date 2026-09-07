#pragma once

#include <QDateTime>
#include <QString>

class QJsonObject;

// 预约：锁定电桩至 expiresAt（默认 15 分钟），超时或取消由服务端释放
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

    // 从服务端 JSON 对象构造 Reservation
    static Reservation fromJson(const QJsonObject &object);
    // 预约状态的中文标签
    static QString statusLabel(const QString &status);
};
