/**
 * @file login_view.h
 * @brief 校验手机号并通过登录接口建立用户会话。
 */
#pragma once

#include "api/api_client.h"
#include "app/session.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

/**
 * @brief 登录页：手机号免密登录（POST /auth/user/login），成功写入 Session 并发 loginSucceeded
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class LoginView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：搭建登录界面并绑定提交动作
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit LoginView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 登录成功信号（Session 已写入用户与令牌），由 MainWindow 接收后跳转主界面
	 */
	void loginSucceeded();

private:
	/**
	 * @brief 校验手机号并提交登录请求（POST /auth/user/login）
	 */
	void submit();

	Session &m_session;					  ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;					  ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLineEdit *m_phoneEdit = nullptr;	  ///< 登录手机号输入框。
	QPushButton *m_loginButton = nullptr; ///< 校验并发送登录请求的按钮。
	QLabel *m_messageLabel = nullptr;	  ///< 请求失败或操作提示标签。
};
