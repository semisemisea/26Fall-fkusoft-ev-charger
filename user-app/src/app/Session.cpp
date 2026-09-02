#include "Session.h"

Session::Session(QObject *parent)
    : QObject(parent)
{
}

void Session::signIn(User user, QString accessToken)
{
    m_user = std::move(user);
    m_accessToken = std::move(accessToken);
    emit signedIn();
}

void Session::signOut()
{
    m_accessToken.clear();
    m_user = User{};
    emit signedOut();
}

void Session::updateBalance(qlonglong balanceFen)
{
    m_user.walletBalanceFen = balanceFen;
    emit userChanged();
}

void Session::updateUser(const User &user)
{
    m_user = user;
    emit userChanged();
}

void Session::setLocation(double latitude, double longitude)
{
    m_latitude = latitude;
    m_longitude = longitude;
}
