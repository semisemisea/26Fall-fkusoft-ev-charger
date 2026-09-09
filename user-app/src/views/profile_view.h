/**
 * @file profile_view.h
 * @brief 展示和编辑个人资料，提供头像上传、充值与历史记录入口。
 */
#pragma once

#include "api/api_client.h"
#include "app/session.h"

#include <QWidget>

class QLabel;

/**
 * @brief 个人中心：资料/头像/余额维护，以及订单、预约、流水等历史记录入口
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class ProfileView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入会话与 API 客户端，搭建资料卡 / 钱包卡 / 菜单列表
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ProfileView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 请求打开历史订单页。
	 */
	void ordersRequested();
	/**
	 * @brief 请求打开预约记录页。
	 */
	void reservationsRequested();
	/**
	 * @brief 请求打开钱包流水页。
	 */
	void transactionsRequested();
	/**
	 * @brief 发出车辆入口意图；当前主窗口未连接此入口。
	 */
	void carRequested();
	/**
	 * @brief 请求打开关于系统页。
	 */
	void aboutRequested();

protected:
	/**
	 * @brief 页面显示时刷新资料与余额
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;
	/**
	 * @brief 拦截头像标签的点击事件以触发更换头像
	 * @param watched 被安装事件过滤器的对象，所有权保持不变。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 * @return 事件已由本控件处理时为 true，否则沿用基类过滤结果。
	 */
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	/**
	 * @brief 用会话中的用户信息刷新昵称、手机号、余额与头像
	 */
	void refreshProfile();
	/**
	 * @brief 按 avatarUrl 下载头像；网络失败回退默认图标，图片解码失败保持现状
	 */
	void loadAvatar();
	/**
	 * @brief 选择本地图片并上传为新头像（POST /me/avatar）
	 */
	void changeAvatar();
	/**
	 * @brief 弹窗输入新昵称并提交 PATCH /me
	 */
	void changeNickname();
	/**
	 * @brief 打开充值对话框
	 */
	void openRecharge();
	/**
	 * @brief 确认后退出登录
	 */
	void signOut();

	Session &m_session;				   ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;				   ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLabel *m_avatarLabel = nullptr;   ///< 头像显示区，同时安装点击过滤器。
	bool m_signingOut = false;		   ///< 防止重复发送退出请求。
	QLabel *m_nicknameLabel = nullptr; ///< 当前昵称标签。
	QLabel *m_phoneLabel = nullptr;	   ///< 脱敏手机号标签。
	QLabel *m_balanceLabel = nullptr;  ///< 钱包余额标签，显示时由分转换为元。
	QString m_loadedAvatarUrl;		   ///< 最近一次尝试加载的头像地址，用于避免重复下载。
};
