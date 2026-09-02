#include "User.h"

#include <QJsonObject>

User User::fromJson(const QJsonObject &object)
{
    User user;
    user.id = object.value(QLatin1String("id")).toInt();
    user.phone = object.value(QLatin1String("phone")).toString();
    user.nickname = object.value(QLatin1String("nickname")).toString();
    user.avatarUrl = object.value(QLatin1String("avatarUrl")).toString();
    user.walletBalanceFen = object.value(QLatin1String("walletBalanceFen")).toInteger();
    user.status = object.value(QLatin1String("status")).toString();
    user.createdAt = object.value(QLatin1String("createdAt")).toString();
    return user;
}
