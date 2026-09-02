#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"

#include <QWidget>

class QLabel;
class QPushButton;

class SettleView : public QWidget
{
    Q_OBJECT

public:
    explicit SettleView(Session &session, ApiClient &api, QWidget *parent = nullptr);

    void open(const Order &order);

signals:
    void settled();
    void dismissed();
    void returnHomeRequested();

private:
    void settle();
    void openRecharge();
    void refreshBalance();
    static QString formatTime(QString isoTime);

    Session &m_session;
    ApiClient &m_api;
    Order m_order;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_stationLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_energyLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_priceLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QPushButton *m_payButton = nullptr;
    QPushButton *m_topUpButton = nullptr;
    QPushButton *m_laterButton = nullptr;
    QPushButton *m_homeButton = nullptr;
};
