#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class ChargePollThread;
class ChargeRingWidget;
class QLabel;
class QPushButton;

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
    void updateDisplay(const Order &order);
    void stopCharging();
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
