/**
 * @file mainwindow.h
 * @brief 装配用户前端依赖并统一协调登录、主标签页和覆盖页导航。
 */
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

/**
 * @brief 总装配器：创建 Session/ApiClient 并注入各页面；管理页面覆盖栈、底部胶囊 Tab 与充电红点角标
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：创建 Session/ApiClient、装配各页面并连接跳转信号
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit MainWindow(QWidget *parent = nullptr);
	/**
	 * @brief 析构函数：释放 UI 资源
	 */
	~MainWindow();

private:
	/**
	 * @brief 构建顶部手机状态栏（时间/信号/电量，定时刷新）
	 */
	void buildStatusBar();
	/**
	 * @brief 构建底部胶囊 Tab 栏（找桩/充电/我的）
	 */
	void buildTabBar();
	/**
	 * @brief 刷新 Tab 图标：选中态高亮 + 充电中红点角标
	 */
	void updateTabIcons();
	/**
	 * @brief 切换到指定 Tab 并返回主内容区
	 * @param index 主标签索引：0 找桩、1 充电、2 我的。
	 */
	void showTab(int index);
	/**
	 * @brief 进入覆盖页面（隐藏 Tab 栏，带淡入动画）
	 * @param page 已经加入主页面堆栈的覆盖页。
	 */
	void enterOverlay(QWidget *page);
	/**
	 * @brief 打开导航页，并记录返回时要回到的页面
	 * @param station 目标电站的界面模型。
	 * @param returnPage 导航返回时恢复的已有页面，不转移所有权。
	 */
	void navigateTo(const class Station &station, QWidget *returnPage);
	/**
	 * @brief 从详情页发起充电下单（POST /orders），成功后跳到充电 Tab
	 * @param charger 目标电桩的界面模型。
	 */
	void startChargingFromDetail(const Charger &charger);
	/**
	 * @brief 从详情页发起预约（POST /reservations），成功后跳到充电 Tab
	 * @param charger 目标电桩的界面模型。
	 */
	void handleReservationFromDetail(const Charger &charger);
	/**
	 * @brief 结算后刷新钱包余额（GET /me）
	 */
	void refreshBalance();

	Ui::MainWindow *ui;								  ///< Designer 生成的界面辅助对象，由 MainWindow 析构显式删除。
	Session *m_session = nullptr;					  ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient *m_api = nullptr;						  ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLabel *m_timeLabel = nullptr;					  ///< 时间显示标签；主窗口显示时钟，结算页显示充电时段。
	QVector<QAbstractButton *> m_tabButtons;		  ///< 底部标签按钮索引，按钮由 Qt 父子树拥有。
	bool m_hasActiveOrder = false;					  ///< 充电标签是否显示活动订单或预约红点。
	QWidget *m_tabContainer = nullptr;				  ///< 找桩、充电、个人中心三页的外层容器。
	QStackedWidget *m_tabStack = nullptr;			  ///< 三个主标签页的切换堆栈。
	StationListView *m_stationListView = nullptr;	  ///< 主窗口装配的找桩首页。
	ChargingTab *m_chargingTab = nullptr;			  ///< 主窗口装配的充电流程协调页。
	ProfileView *m_profileView = nullptr;			  ///< 主窗口装配的个人中心页。
	StationDetailView *m_stationDetailView = nullptr; ///< 复用的站点详情覆盖页。
	NavigationView *m_navigationView = nullptr;		  ///< 复用的路线导航覆盖页。
	QWidget *m_navigationReturnPage = nullptr;		  ///< 导航返回目标的非拥有指针，指向已存在的主容器或详情页。
};
