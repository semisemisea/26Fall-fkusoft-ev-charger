#pragma once

#include <QtGlobal>
#include <QString>

class QJsonObject;

struct User
{
    int id = 0;
    QString phone;
    QString nickname;
    QString avatarUrl;
    qlonglong walletBalanceFen = 0;
    QString status;
    QString createdAt;

    static User fromJson(const QJsonObject &object);
};
