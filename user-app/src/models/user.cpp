/**
 * @file user.cpp
 * @brief 解析用户资料、头像存在标志与以分为单位的钱包余额。
 */
#include "user.h"

#include <QJsonObject>

/**
 * @details 逐字段解析 JSON；余额 walletBalanceFen 为整数（分）
 */
User User::fromJson(const QJsonObject &object) {
	User user;
	user.id = object.value(QLatin1String("id")).toInt();
	user.phone = object.value(QLatin1String("phone")).toString();
	user.nickname = object.value(QLatin1String("nickname")).toString();
	user.avatarUrl = object.value(QLatin1String("hasAvatar")).toBool()
						 ? QStringLiteral("/me/avatar")
						 : QString();
	user.walletBalanceFen = object.value(QLatin1String("walletBalanceFen")).toInteger();
	user.status = object.value(QLatin1String("status")).toString();
	user.createdAt = object.value(QLatin1String("createdAt")).toString();
	return user;
}
