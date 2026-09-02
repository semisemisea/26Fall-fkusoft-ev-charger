#pragma once

#include "models/User.h"

#include <QObject>
#include <QString>

class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject *parent = nullptr);

    [[nodiscard]] bool isLoggedIn() const { return !m_accessToken.isEmpty(); }
    [[nodiscard]] const User &user() const { return m_user; }
    [[nodiscard]] const QString &accessToken() const { return m_accessToken; }

    void signIn(User user, QString accessToken);
    void signOut();
    void updateBalance(qlonglong balanceFen);

signals:
    void signedIn();
    void signedOut();
    void userChanged();

private:
    User m_user;
    QString m_accessToken;
};
