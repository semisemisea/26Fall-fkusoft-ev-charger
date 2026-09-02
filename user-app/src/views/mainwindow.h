#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QMainWindow>

class ChargingView;
class NavigationView;
class ProfileView;
class SettleView;

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
    void checkActiveOrder();

    Ui::MainWindow *ui;
    Session *m_session = nullptr;
    ApiClient *m_api = nullptr;
    ChargingView *m_chargingView = nullptr;
    SettleView *m_settleView = nullptr;
    QWidget *m_navigationReturnPage = nullptr;
};
