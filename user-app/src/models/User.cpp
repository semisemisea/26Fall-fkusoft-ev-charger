#include "User.h"

#include <QJsonObject>

// 逐字段解析 JSON；余额 walletBalanceFen 为整数（分）
User User::fromJson(const QJsonObject &object)
{
    User user;
    user.id = object.value(QLatin1String("id")).toInt();
    user.phone = object.value(QLatin1String("phone")).toString();
    user.nickname = object.value(QLatin1String("nickname")).toString();
	user.avatarUrl = object.value(QLatin1String("avatarUrl")).toString(); // 头像 URL 地址
	user.walletBalanceFen = object.value(QLatin1String("walletBalanceFen")).toInteger(); // 钱包余额
	user.status = object.value(QLatin1String("status")).toString(); // 用户状态（active/frozen）
	user.createdAt = object.value(QLatin1String("createdAt")).toString(); // 帐号注册时间
    return user;
}
