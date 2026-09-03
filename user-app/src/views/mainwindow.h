#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Charger.h"

#include <QMainWindow>
#include <QVector>

class ChargingTab;
class QLabel;
class ProfileView;
class QStackedWidget;
class StationDetailView;
class StationListView;
class NavigationView;
class QAbstractButton;

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void buildStatusBar();
    void buildTabBar();
    void updateTabIcons();
    void showTab(int index);
    void enterOverlay(QWidget *page);
    void navigateTo(const class Station &station, QWidget *returnPage);
    void startChargingFromDetail(const Charger &charger);
    void handleReservationFromDetail(const Charger &charger);
    void refreshBalance();

    Ui::MainWindow *ui;
    Session *m_session = nullptr;
    ApiClient *m_api = nullptr;
    QLabel *m_timeLabel = nullptr;
    QVector<QAbstractButton *> m_tabButtons;
    bool m_hasActiveOrder = false;
    QWidget *m_tabContainer = nullptr;
    QStackedWidget *m_tabStack = nullptr;
    StationListView *m_stationListView = nullptr;
    ChargingTab *m_chargingTab = nullptr;
    ProfileView *m_profileView = nullptr;
    StationDetailView *m_stationDetailView = nullptr;
    NavigationView *m_navigationView = nullptr;
    QWidget *m_navigationReturnPage = nullptr;
};
