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
    void updateUser(const User &user);
    void setLocation(double latitude, double longitude);

    [[nodiscard]] double latitude() const { return m_latitude; }
    [[nodiscard]] double longitude() const { return m_longitude; }

signals:
    void signedIn();
    void signedOut();
    void userChanged();

private:
    User m_user;
    QString m_accessToken;
    double m_latitude = 38.914;
    double m_longitude = 121.614;
};
