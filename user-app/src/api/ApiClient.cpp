#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QUrl>

namespace {
constexpr int kRequestTimeoutMs = 10'000;
}

ApiClient::ApiClient(QString baseUrl, QObject *parent)
    : QObject(parent)
    , m_baseUrl(std::move(baseUrl))
    , m_manager(new QNetworkAccessManager(this))
{
}

void ApiClient::setAccessToken(const QString &token)
{
    m_accessToken = token;
}

void ApiClient::get(const QString &path, Success onSuccess, Failure onFailure)
{
    send(path, false, nullptr, std::move(onSuccess), std::move(onFailure));
}

void ApiClient::post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure)
{
    send(path, true, &body, std::move(onSuccess), std::move(onFailure));
}

void ApiClient::send(const QString &path, bool isPost, const QJsonObject *body, Success onSuccess, Failure onFailure)
{
    QNetworkRequest request{QUrl(m_baseUrl + path)};
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    if (!m_accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    }

    QByteArray payload;
    if (body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }

    QNetworkReply *reply = isPost ? m_manager->post(request, payload) : m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)] {
                reply->deleteLater();

                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();

                if (status == 0) {
                    ApiError error;
                    error.message = reply->errorString();
                    onFailure(error);
                    return;
                }
                if (reply->error() == QNetworkReply::NoError) {
                    onSuccess(root.value(QLatin1String("data")), root.value(QLatin1String("meta")).toObject());
                    return;
                }

                ApiError error;
                error.httpStatus = status;
                const QJsonObject err = root.value(QLatin1String("error")).toObject();
                error.code = err.value(QLatin1String("code")).toString(QStringLiteral("HTTP_%1").arg(status));
                error.message = err.value(QLatin1String("message")).toString(reply->errorString());
                error.requestId = root.value(QLatin1String("meta")).toObject().value(QLatin1String("requestId")).toString();
                onFailure(error);
            });
}
