/**
 * @file Session.h
 * @brief 保存进程内登录用户、钱包余额与当前位置，并广播会话变化。
 */
#pragma once

#include "models/User.h"

#include <QObject>
#include <QString>

/**
 * @brief 登录态容器：保存当前用户、访问令牌与定位；状态变化经信号广播，页面监听信号刷新
 */
class Session : public QObject {
	Q_OBJECT

public:
	/**
	 * @brief 构造未登录会话，默认查询位置为大连市中心。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit Session(QObject *parent = nullptr);

	/**
	 * @brief 判断当前是否持有非空访问令牌；不验证服务端有效期。
	 * @return 有非空令牌时为 true；不代表令牌已在服务端验证。
	 */
	[[nodiscard]] bool isLoggedIn() const { return !m_accessToken.isEmpty(); }
	/**
	 * @brief 读取会话中保存的用户快照。
	 * @return 会话内部用户的只读引用，会话更新后内容可变化。
	 */
	[[nodiscard]] const User &user() const { return m_user; } ///< 当前用户的值语义快照。
	/**
	 * @brief 读取当前保存的访问令牌。
	 * @return 对象内部访问令牌的只读引用。
	 */
	[[nodiscard]] const QString &accessToken() const { return m_accessToken; } ///< 当前访问令牌，空值表示未登录。

	/**
	 * @brief 登录：保存用户与令牌并广播 signedIn
	 * @param user 需要写入会话的用户资料。
	 * @param accessToken 登录接口返回的访问令牌。
	 */
	void signIn(User user, QString accessToken);
	/**
	 * @brief 登出：清空令牌与用户并广播 signedOut
	 */
	void signOut();
	/**
	 * @brief 仅更新钱包余额（单位：分），广播 userChanged
	 * @param balanceFen 最新钱包余额，单位为分。
	 */
	void updateBalance(qlonglong balanceFen);
	/**
	 * @brief 整体替换当前用户资料，广播 userChanged
	 * @param user 需要写入会话的用户资料。
	 */
	void updateUser(const User &user);
	/**
	 * @brief 更新定位（经纬度），用于按距离查询附近电站
	 * @param latitude 纬度，单位为度。
	 * @param longitude 经度，单位为度。
	 */
	void setLocation(double latitude, double longitude);

	/**
	 * @brief 读取当前纬度。
	 * @return 当前纬度，单位为度。
	 */
	[[nodiscard]] double latitude() const { return m_latitude; } ///< 当前查询位置的纬度，默认指向大连市中心。
	/**
	 * @brief 读取当前经度。
	 * @return 当前经度，单位为度。
	 */
	[[nodiscard]] double longitude() const { return m_longitude; } ///< 当前查询位置的经度，默认指向大连市中心。

signals:
	/**
	 * @brief 用户和令牌写入后发出，供装配层同步网络凭据。
	 */
	void signedIn();
	/**
	 * @brief 用户和令牌清空后发出，供装配层返回登录页。
	 */
	void signedOut();
	/**
	 * @brief 资料或余额更新后发出，供已创建页面刷新显示。
	 */
	void userChanged();

private:
	User m_user;				  ///< 当前用户的值语义快照。
	QString m_accessToken;		  ///< 当前访问令牌，空值表示未登录。
	double m_latitude = 38.914;	  ///< 当前查询位置的纬度，默认指向大连市中心。
	double m_longitude = 121.614; ///< 当前查询位置的经度，默认指向大连市中心。
};
