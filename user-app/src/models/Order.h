#pragma once

#include <QString>
#include <QtGlobal>

class QJsonObject;

// 充电订单：账单金额由服务端算定（电量 × 单价），客户端不覆盖
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

    // 从服务端 JSON 对象构造 Order
    static Order fromJson(const QJsonObject &object);
    // 订单状态的中文标签
    static QString statusLabel(const QString &status);
};
