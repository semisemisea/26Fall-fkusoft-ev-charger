/** @file
 * @brief 主窗口的左侧品牌导航与右侧页面栈，创建并管理四个运营页面。
 */
#pragma once

// 管理后台主窗口:左侧导航 + 右侧 QStackedWidget 页面栈。
// 布局参考 Qt 官方 Thermostat 示例的深色卡片视觉,用 Widgets + QSS 实现。

#include <QHBoxLayout>
#include <QMainWindow>

#include "api/apiclient.h"

class QLabel;
class QListWidget;
class QStackedWidget;
class SalesPage;
class ChargerStatusPage;
class StationPage;
class UserPage;

/// @brief 管理后台导航容器；子页面由 Qt 对象树管理，共享外部 ApiClient。
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	/** @brief 建立中央水平布局、导航与四个页面，并连接导航索引切换。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit MainWindow(ops::ApiClient *api, QWidget *parent = nullptr);
	/** @brief 释放自身辅助资源；Qt 自动释放所属子对象。
	 */
	~MainWindow();

	// 更新已登录状态的展示
	/** @brief 更新状态栏为已登录提示。
	 */
	void resumeAfterLogin();

private:
	/** @brief 构造左侧品牌、导航和管理员信息，并加入中央布局。
	 * @param layout 接收侧边栏的中央水平布局。
	 */
	void buildSidebar(QHBoxLayout *layout);
	/** @brief 按导航索引创建对应页面。
	 * @param index 导航项的零基索引。
	 * @return 新建页面，由页面栈接管；未知索引返回空白 QWidget。
	 */
	QWidget *createPage(int index);

	ops::ApiClient *m_api; ///< 非拥有的共享客户端，须比界面对象存活更久。

	QListWidget *m_navList = nullptr;				  ///< 左侧导航列表；由 Qt 对象树管理。
	QStackedWidget *m_stack = nullptr;				  ///< 页面堆栈；由 Qt 对象树管理。
	QLabel *m_userLabel = nullptr;					  ///< 管理员身份标签；由 Qt 对象树管理。
	SalesPage *m_salesPage = nullptr;				  ///< 营收页面；由 Qt 对象树管理。
	ChargerStatusPage *m_chargerStatusPage = nullptr; ///< 状态统计页面；由 Qt 对象树管理。
	StationPage *m_stationPage = nullptr;			  ///< 电站管理页面；由 Qt 对象树管理。
	UserPage *m_userPage = nullptr;					  ///< 用户管理页面；由 Qt 对象树管理。
};
