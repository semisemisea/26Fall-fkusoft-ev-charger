#pragma once

#include "models/User.h"

#include <QObject>
#include <QString>

// 登录态容器：保存当前用户、访问令牌与定位；状态变化经信号广播，页面监听信号刷新
class Session : public QObject {
	Q_OBJECT

public:
	explicit Session(QObject *parent = nullptr);

	// 是否已登录、当前用户与访问令牌（自明的读取访问器）
	[[nodiscard]] bool isLoggedIn() const { return !m_accessToken.isEmpty(); }
	[[nodiscard]] const User &user() const { return m_user; }
	[[nodiscard]] const QString &accessToken() const { return m_accessToken; }

	// 登录：保存用户与令牌并广播 signedIn
	void signIn(User user, QString accessToken);
	// 登出：清空令牌与用户并广播 signedOut
	void signOut();
	// 仅更新钱包余额（单位：分），广播 userChanged
	void updateBalance(qlonglong balanceFen);
	// 整体替换当前用户资料，广播 userChanged
	void updateUser(const User &user);
	// 更新定位（经纬度），用于按距离查询附近电站
	void setLocation(double latitude, double longitude);

	// 当前定位
	[[nodiscard]] double latitude() const { return m_latitude; }
	[[nodiscard]] double longitude() const { return m_longitude; }

signals:
	void signedIn();
	void signedOut();
	void userChanged();

private:
	User m_user;
	QString m_accessToken;
	double m_latitude = 38.914;
	double m_longitude = 121.614;
};
