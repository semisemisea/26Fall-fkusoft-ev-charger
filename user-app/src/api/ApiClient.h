#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QJsonObject;
class QNetworkAccessManager;

struct ApiError {
    QString code = QStringLiteral("NETWORK_ERROR");
    QString message;
    int httpStatus = 0;
    QString requestId;
};

class ApiClient : public QObject
{
    Q_OBJECT

public:
    using Success = std::function<void(const QJsonValue &data, const QJsonObject &meta)>;
    using Failure = std::function<void(const ApiError &error)>;

    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);

    void setAccessToken(const QString &token);
    [[nodiscard]] const QString &accessToken() const { return m_accessToken; }

    void get(const QString &path, Success onSuccess, Failure onFailure);
    void post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);

private:
    void send(const QString &path, bool isPost, const QJsonObject *body, Success onSuccess, Failure onFailure);

    QString m_baseUrl;
    QString m_accessToken;
    QNetworkAccessManager *m_manager = nullptr;
};
