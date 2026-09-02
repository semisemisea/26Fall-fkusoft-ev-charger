#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Station.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWebEngineView;

class NavigationView : public QWidget
{
    Q_OBJECT

public:
    explicit NavigationView(Session &session, ApiClient &api, QWidget *parent = nullptr);

    void open(const Station &station);

signals:
    void backRequested();

private:
    void requestRoute();

    Session &m_session;
    ApiClient &m_api;
    Station m_station;
    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_navigateButton = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWebEngineView *m_webView = nullptr;
};
