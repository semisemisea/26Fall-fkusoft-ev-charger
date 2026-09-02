#pragma once

#include "api/ApiClient.h"
#include "models/Charger.h"
#include "models/Station.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

class StationDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit StationDetailView(ApiClient &api, QWidget *parent = nullptr);

    void open(const Station &station);

signals:
    void backRequested();
    void chargeRequested(const Charger &charger);
    void navigateRequested(const Station &station);

private:
    void loadChargers();

    ApiClient &m_api;
    Station m_station;
    QPushButton *m_backButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_chargersLayout = nullptr;
};
