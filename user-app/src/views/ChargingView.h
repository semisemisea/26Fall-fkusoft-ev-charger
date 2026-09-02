#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

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
    void poll();
    void updateDisplay(const Order &order);
    void stopCharging();

    ApiClient &m_api;
    Order m_order;
    QTimer *m_pollTimer = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_energyLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_amountLabel = nullptr;
    QLabel *m_orderLabel = nullptr;
    QPushButton *m_stopButton = nullptr;
};
