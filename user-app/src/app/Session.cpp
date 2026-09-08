#include "Session.h"
#include <evcharger/logging.h>

Q_LOGGING_CATEGORY(userSession, "evcharger.user.session", QtInfoMsg)

Session::Session(QObject *parent)
	: QObject(parent) {
	setObjectName(QStringLiteral("userSession"));
	EV_LOG_INFO(userSession, this) << "Session initialized";
}

// 登录：移动语义接收用户与令牌，随后广播 signedIn 触发界面切换
void Session::signIn(User user, QString accessToken) {
	m_user = std::move(user);
	m_accessToken = std::move(accessToken);
	EV_LOG_INFO(userSession, this) << "Sign-in completed";
	emit signedIn();
}

// 登出：清空令牌并重置用户为默认值
void Session::signOut() {
	m_accessToken.clear();
	m_user = User{};
	EV_LOG_INFO(userSession, this) << "Sign-out completed";
	emit signedOut();
}

// 充值/支付后只改余额，避免整包刷新用户资料
void Session::updateBalance(qlonglong balanceFen) {
	EV_LOG_INFO(userSession, this) << "Wallet balance refreshed";
	m_user.walletBalanceFen = balanceFen;
	emit userChanged();
}

// 资料编辑成功后整体替换用户
void Session::updateUser(const User &user) {
	EV_LOG_INFO(userSession, this) << "Profile updated";
	m_user = user;
	emit userChanged();
}

// 更新定位；不发信号，由调用方触发重新查询
void Session::setLocation(double latitude, double longitude) {
	EV_LOG_INFO(userSession, this) << "Location updated";
	m_latitude = latitude;
	m_longitude = longitude;
}
