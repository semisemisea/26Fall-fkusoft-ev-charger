#pragma once

#include "api/ApiClient.h"
#include "models/Charger.h"
#include "models/Station.h"

#include <QWidget>
#include <QPixmap>
#include <QVector>

class QLabel;
class QPushButton;
class QFrame;
class Spinner;
class QVBoxLayout;
class QResizeEvent;

class StationDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit StationDetailView(ApiClient &api, QWidget *parent = nullptr);

    void open(const Station &station);

signals:
    void backRequested();
    void chargeRequested(const Charger &charger);
    void reservationRequested(const Charger &charger);
    void navigateRequested(const Station &station);

private:
    void loadChargers();
    void updateBottomButtons();
    bool eventFilter(QObject *obj, QEvent *event) override;

    ApiClient &m_api;
    Station m_station;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_navButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_distanceLabel = nullptr;
    QLabel *m_priceLabel = nullptr;
    QLabel *m_availabilityLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_chargersLayout = nullptr;
    QPixmap m_bgPixmap;
    QWidget *m_bgSpacer = nullptr;

    QPushButton *m_reserveButton = nullptr;
    QPushButton *m_chargeButton = nullptr;

    QVector<Charger> m_chargers;
    Charger m_selectedCharger;
    bool m_hasSelection = false;
    QFrame *m_selectedRow = nullptr;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};