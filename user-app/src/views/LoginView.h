#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginView : public QWidget
{
    Q_OBJECT

public:
    explicit LoginView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    void loginSucceeded();

private:
    void submit();

    Session &m_session;
    ApiClient &m_api;
    QLineEdit *m_phoneEdit = nullptr;
    QPushButton *m_loginButton = nullptr;
    QLabel *m_messageLabel = nullptr;
};
