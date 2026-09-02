#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"

#include <QWidget>
#include <functional>

class ChargingView;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class SettleView;

class ChargingTab : public QWidget
{
    Q_OBJECT

public:
    explicit ChargingTab(Session &session, ApiClient &api, QWidget *parent = nullptr);

    void checkActiveOrder();
    void showCharging(const Order &order);
    void showSettlement(const Order &order);

signals:
    void orderSettled();
    void returnHomeRequested();

private:
    void buildPreparePage();
    void showPrepare();
    void startWithCode();
    void fetchNearbyStations();
    void tryNextCandidate();
    void createOrder(int chargerId);
    void fail(const QString &message);

    Session &m_session;
    ApiClient &m_api;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_preparePage = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLineEdit *m_codeEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    ChargingView *m_chargingView = nullptr;
    SettleView *m_settleView = nullptr;
    QString m_pendingCode;
    QVector<int> m_candidateStationIds;
    int m_candidateIndex = 0;
};
