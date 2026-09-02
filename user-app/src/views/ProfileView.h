#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class ProfileView : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshProfile();
    void loadAvatar();
    void changeAvatar();
    void changeNickname();
    void topUp();
    void loadTransactions();

    Session &m_session;
    ApiClient &m_api;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nicknameLabel = nullptr;
    QLabel *m_phoneLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QListWidget *m_transactionList = nullptr;
    QString m_loadedAvatarUrl;
};
