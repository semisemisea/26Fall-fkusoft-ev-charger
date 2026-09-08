#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class ChargePollThread;
class BatteryWaveWidget;
class QLabel;
class QPushButton; class QTimer;

// 充电进行页：ChargePollThread 每 5 秒轮询 GET /orders/{id}，刷新电池、卡片与金额
class ChargingView : public QWidget
{
    Q_OBJECT

public:
    explicit ChargingView(ApiClient &api, QWidget *parent = nullptr);
    void open(const Order &order);

signals:
    void orderStopped(const Order &order);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    // 汽车充满时的总电量（度）
    static constexpr double kFullBatteryKwh = 1200.0;
    // 每度电可行驶公里数（km/度）
    static constexpr int kKmPerKwh = 10;
    // 充入 1 度电所需时长（分钟/度）
    static constexpr int kMinutesPerKwh = 1;

    void updateDisplay(const Order &order);
    void stopCharging();
    static QString formatDuration(int minutes);

    ApiClient &m_api;
    Order m_order;
    ChargePollThread *m_pollThread = nullptr;
    BatteryWaveWidget *m_batteryWidget = nullptr;
    QLabel *m_rangeValueLabel = nullptr;
    QLabel *m_energyValueLabel = nullptr;
    QLabel *m_typeValueLabel = nullptr;
    QLabel *m_remainValueLabel = nullptr;
    QLabel *m_feeValueLabel = nullptr;
    QPushButton *m_stopButton = nullptr;
};