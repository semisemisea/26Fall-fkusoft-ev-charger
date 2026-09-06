#pragma once

#include "api/ApiClient.h"
#include "models/Charger.h"
#include "models/Station.h"

#include <QWidget>
#include <QPixmap>

class QLabel;
class QPushButton;
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

    ApiClient &m_api;
    Station m_station;
    QPushButton *m_backButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_chargersLayout = nullptr;
    QPixmap m_bgPixmap;
    QWidget *m_bgSpacer = nullptr;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};
