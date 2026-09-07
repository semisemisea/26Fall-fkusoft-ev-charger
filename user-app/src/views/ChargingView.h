#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class ChargePollThread;
class ChargeRingWidget;
class QLabel;
class QPushButton;

// 充电进行页：ChargePollThread 每 5 秒轮询 GET /orders/{id}，刷新充电环、电量与金额
class ChargingView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入 API 客户端并搭建充电进行页界面
    explicit ChargingView(ApiClient &api, QWidget *parent = nullptr);

    // 打开指定订单：刷新显示并启动 5 秒轮询线程
    void open(const Order &order);

signals:
    // 停止充电成功（服务端已生成账单）后发出
    void orderStopped(const Order &order);

protected:
    // 页面隐藏时停止轮询线程，避免后台无效请求
    void hideEvent(QHideEvent *event) override;

private:
    // 按最新订单数据刷新充电环与各数值标签
    void updateDisplay(const Order &order);
    // 确认后调用 POST /orders/{id}/stop 结束充电
    void stopCharging();
    // 将分钟格式化为 HH:mm:ss 时长文本
    static QString formatDuration(int minutes);

    ApiClient &m_api;
    Order m_order;
    ChargePollThread *m_pollThread = nullptr;
    ChargeRingWidget *m_ringWidget = nullptr;
    QLabel *m_headerLabel = nullptr;
    QLabel *m_energyLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_amountLabel = nullptr;
    QLabel *m_orderLabel = nullptr;
    QPushButton *m_stopButton = nullptr;
};
