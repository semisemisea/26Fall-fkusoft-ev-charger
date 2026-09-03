#pragma once

#include <QJsonObject>
#include <QThread>
#include <atomic>

class ChargePollThread : public QThread
{
    Q_OBJECT

public:
    explicit ChargePollThread(QObject *parent = nullptr);

    void configure(int orderId, const QString &baseUrl, const QString &token);
    void requestStop();

signals:
    void meterUpdated(const QJsonObject &order);

protected:
    void run() override;

private:
    int m_orderId = 0;
    QString m_baseUrl;
    QString m_token;
    std::atomic<bool> m_stop{false};
};
