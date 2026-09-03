#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QWidget>

class QLabel;

class ProfileView : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    void ordersRequested();
    void reservationsRequested();
    void transactionsRequested();
    void carRequested();
    void aboutRequested();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshProfile();
    void loadAvatar();
    void changeAvatar();
    void changeNickname();
    void openRecharge();
    void signOut();

    Session &m_session;
    ApiClient &m_api;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nicknameLabel = nullptr;
    QLabel *m_phoneLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QString m_loadedAvatarUrl;
};
