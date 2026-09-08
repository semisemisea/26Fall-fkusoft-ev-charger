/**
 * @file Session.cpp
 * @brief 保存进程内登录用户、钱包余额与当前位置，并广播会话变化。
 */
#include "Session.h"

/**
 * @details 仅建立 QObject 父子关系，用户、令牌和默认坐标使用头文件中的初始值。
 */
Session::Session(QObject *parent)
	: QObject(parent) {
}

/**
 * @details 登录：移动语义接收用户与令牌，随后广播 signedIn 触发界面切换
 */
void Session::signIn(User user, QString accessToken) {
	m_user = std::move(user);
	m_accessToken = std::move(accessToken);
	emit signedIn();
}

/**
 * @details 登出：清空令牌并重置用户为默认值
 */
void Session::signOut() {
	m_accessToken.clear();
	m_user = User{};
	emit signedOut();
}

/**
 * @details 充值/支付后只改余额，避免整包刷新用户资料
 */
void Session::updateBalance(qlonglong balanceFen) {
	m_user.walletBalanceFen = balanceFen;
	emit userChanged();
}

/**
 * @details 资料编辑成功后整体替换用户
 */
void Session::updateUser(const User &user) {
	m_user = user;
	emit userChanged();
}

/**
 * @details 更新定位；不发信号，由调用方触发重新查询
 */
void Session::setLocation(double latitude, double longitude) {
	m_latitude = latitude;
	m_longitude = longitude;
}
