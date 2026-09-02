#pragma once

#include "api/ApiClient.h"
#include "models/Station.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

class StationListView : public QWidget
{
    Q_OBJECT

public:
    explicit StationListView(ApiClient &api, QWidget *parent = nullptr);

signals:
    void stationSelected(const Station &station);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void reload();

    ApiClient &m_api;
    QComboBox *m_locationCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
};
