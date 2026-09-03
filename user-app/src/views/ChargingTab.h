#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"
#include "models/Reservation.h"

#include <QWidget>
#include <functional>

class ChargingView;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class SettleView;

class ChargingTab : public QWidget
{
    Q_OBJECT

public:
    explicit ChargingTab(Session &session, ApiClient &api, QWidget *parent = nullptr);

    void checkActiveOrder();
    void showCharging(const Order &order);
    void showSettlement(const Order &order);
    void showReservation(const Reservation &reservation);

signals:
    void orderSettled();
    void returnHomeRequested();
    void activeOrderChanged(bool hasActiveOrder);

private:
    void buildPreparePage();
    void buildReservationPage();
    void showPrepare();
    void startWithCode();
    void fetchNearbyStations();
    void tryNextCandidate();
    void createOrder(int chargerId);
    void fail(const QString &message);
    void checkActiveReservation();
    void startFromReservation();
    void cancelReservation();
    void updateCountdown();
    void setReservationBusy(bool busy);

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

    QWidget *m_reservationPage = nullptr;
    QLabel *m_reservationStationLabel = nullptr;
    QLabel *m_reservationChargerLabel = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QLabel *m_reservationHintLabel = nullptr;
    QPushButton *m_reservationStartButton = nullptr;
    QPushButton *m_reservationCancelButton = nullptr;
    QTimer *m_countdownTimer = nullptr;
    Reservation m_reservation;
};
