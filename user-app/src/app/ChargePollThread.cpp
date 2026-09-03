#include "ChargePollThread.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int kPollIntervalMs = 5000;
constexpr int kRequestTimeoutMs = 10000;
}

ChargePollThread::ChargePollThread(QObject *parent)
    : QThread(parent)
{
    qRegisterMetaType<QJsonObject>();
}

void ChargePollThread::configure(int orderId, const QString &baseUrl, const QString &token)
{
    m_orderId = orderId;
    m_baseUrl = baseUrl;
    m_token = token;
    m_stop.store(false);
}

void ChargePollThread::requestStop()
{
    m_stop.store(true);
    quit();
    wait();
}

void ChargePollThread::run()
{
    QNetworkAccessManager manager;

    while (!m_stop.load()) {
        QNetworkRequest request(QUrl(QStringLiteral("%1/orders/%2").arg(m_baseUrl).arg(m_orderId)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_token).toUtf8());
        request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
        request.setTransferTimeout(kRequestTimeoutMs);

        QEventLoop loop;
        QNetworkReply *reply = manager.get(request);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (!m_stop.load() && reply->error() == QNetworkReply::NoError) {
            const QJsonObject envelope = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject order = envelope.value(QLatin1String("data")).toObject();
            if (order.value(QLatin1String("status")).toString() == QLatin1String("charging")) {
                emit meterUpdated(order);
            }
        }
        reply->deleteLater();

        for (int waited = 0; waited < kPollIntervalMs && !m_stop.load(); waited += 250) {
            msleep(250);
        }
    }
}
