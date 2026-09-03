#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Station.h"

#include <QWidget>
#include <QVector>

class ComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class Spinner;
class QVBoxLayout;
class StationCard;

class StationListView : public QWidget
{
    Q_OBJECT

public:
    explicit StationListView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    void stationSelected(const Station &station);
    void navigateRequested(const Station &station);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void reload();
    void applyFilter();
    void loadRecommendation();

    Session &m_session;
    ApiClient &m_api;
    ComboBox *m_locationCombo = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_bannerButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QVector<StationCard *> m_cards;
    Station m_recommendedStation;
    bool m_hasRecommendation = false;
    bool m_listAnimated = false;
};
