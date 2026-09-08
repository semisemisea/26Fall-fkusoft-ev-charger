/**
 * @file User.h
 * @brief 解析用户资料、头像存在标志与以分为单位的钱包余额。
 */
#pragma once

#include <QtGlobal>
#include <QString>

class QJsonObject;

/**
 * @brief 用户：资料与钱包余额（金额单位为分，见 docs/apis.md）
 */
struct User {
	int id = 0;						///< 服务端分配的实体标识；默认 0 表示尚未填充。
	QString phone;					///< 用户手机号。
	QString nickname;				///< 界面显示的用户昵称。
	QString avatarUrl;				///< 有头像时为 /me/avatar，否则为空；由 hasAvatar 字段推导。
	qlonglong walletBalanceFen = 0; ///< 钱包余额，单位为整数分。
	QString status;					///< 用户账户状态字符串，例如 active 或 frozen。
	QString createdAt;				///< 服务端创建时间的原始 ISO 文本。

	/**
	 * @brief 从服务端 JSON 对象构造 User
	 * @param object 服务端 JSON 对象；缺失字段采用 Qt 转换的默认值。
	 * @return 按接口字段映射后的值对象，不进行业务合法性校验。
	 */
	static User fromJson(const QJsonObject &object);
};
