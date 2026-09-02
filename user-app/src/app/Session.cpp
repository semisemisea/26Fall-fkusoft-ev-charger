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
