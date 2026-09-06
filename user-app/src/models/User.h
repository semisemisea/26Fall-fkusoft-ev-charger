#pragma once

#include <QtGlobal>
#include <QString>

class QJsonObject;

// 用户：资料与钱包余额（金额单位为分，见 docs/apis.md）
struct User
{
    int id = 0;
    QString phone;
    QString nickname;
    QString avatarUrl;
    qlonglong walletBalanceFen = 0;
    QString status;
    QString createdAt;

    // 从服务端 JSON 对象构造 User
    static User fromJson(const QJsonObject &object);
};
