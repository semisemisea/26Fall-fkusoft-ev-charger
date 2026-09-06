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

// 总装配器：创建 Session/ApiClient 并注入各页面；管理页面覆盖栈、底部胶囊 Tab 与充电红点角标
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数：创建 Session/ApiClient、装配各页面并连接跳转信号
    explicit MainWindow(QWidget *parent = nullptr);
    // 析构函数：释放 UI 资源
    ~MainWindow();

private:
    // 构建顶部手机状态栏（时间/信号/电量，定时刷新）
    void buildStatusBar();
    // 构建底部胶囊 Tab 栏（找桩/充电/我的）
    void buildTabBar();
    // 刷新 Tab 图标：选中态高亮 + 充电中红点角标
    void updateTabIcons();
    // 切换到指定 Tab 并返回主内容区
    void showTab(int index);
    // 进入覆盖页面（隐藏 Tab 栏，带淡入动画）
    void enterOverlay(QWidget *page);
    // 打开导航页，并记录返回时要回到的页面
    void navigateTo(const class Station &station, QWidget *returnPage);
    // 从详情页发起充电下单（POST /orders），成功后跳到充电 Tab
    void startChargingFromDetail(const Charger &charger);
    // 从详情页发起预约（POST /reservations），成功后跳到充电 Tab
    void handleReservationFromDetail(const Charger &charger);
    // 结算后刷新钱包余额（GET /me）
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
