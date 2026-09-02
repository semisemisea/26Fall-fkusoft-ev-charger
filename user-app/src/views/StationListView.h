#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
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
    explicit StationListView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    void stationSelected(const Station &station);
    void checkActiveOrderRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void reload();
    void refreshWelcome();

    Session &m_session;
    ApiClient &m_api;
    QComboBox *m_locationCombo = nullptr;
    QLabel *m_welcomeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
};
