#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QHttpMultiPart;
class QJsonObject;
class QNetworkAccessManager;
class QUrl;

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
    using DownloadSuccess = std::function<void(const QByteArray &payload)>;

    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);

    void setAccessToken(const QString &token);
    [[nodiscard]] const QString &accessToken() const { return m_accessToken; }

    void get(const QString &path, Success onSuccess, Failure onFailure);
    void post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
    void patch(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
    void upload(const QString &path, QHttpMultiPart *multiPart, Success onSuccess, Failure onFailure);
    void download(const QUrl &url, DownloadSuccess onSuccess, Failure onFailure);

private:
    enum class Verb { Get, Post, Patch };

    void send(const QString &path, Verb verb, const QJsonObject *body, Success onSuccess, Failure onFailure);

    QString m_baseUrl;
    QString m_accessToken;
    QNetworkAccessManager *m_manager = nullptr;
};
