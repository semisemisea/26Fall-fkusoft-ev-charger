#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

// 登录页：手机号免密登录（POST /auth/user/login），成功写入 Session 并发 loginSucceeded
class LoginView : public QWidget
{
    Q_OBJECT

public:
    // 构造函数：搭建登录界面并绑定提交动作
    explicit LoginView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    // 登录成功信号（Session 已写入用户与令牌），由 MainWindow 接收后跳转主界面
    void loginSucceeded();

private:
    // 校验手机号并提交登录请求（POST /auth/user/login）
    void submit();

    Session &m_session;
    ApiClient &m_api;
    QLineEdit *m_phoneEdit = nullptr;
    QPushButton *m_loginButton = nullptr;
    QLabel *m_messageLabel = nullptr;
};
