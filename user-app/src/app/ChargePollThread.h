#pragma once

#include <QJsonObject>
#include <QThread>
#include <atomic>

// 充电订单轮询线程：每 5 秒拉取 GET /orders/{id}，meterUpdated 信号跨线程回主线程刷新界面
class ChargePollThread : public QThread
{
    Q_OBJECT

public:
    explicit ChargePollThread(QObject *parent = nullptr);

    // 配置轮询目标：订单 ID、服务地址与 Bearer 令牌；启动线程前调用
    void configure(int orderId, const QString &baseUrl, const QString &token);
    // 请求停止并阻塞等待线程退出
    void requestStop();

signals:
    // 充电中订单的最新数据（跨线程队列投递到主线程）
    void meterUpdated(const QJsonObject &order);

protected:
    // 线程主体：循环拉取订单并按间隔休眠
    void run() override;

private:
    int m_orderId = 0;
    QString m_baseUrl;
    QString m_token;
    std::atomic<bool> m_stop{false};
};
