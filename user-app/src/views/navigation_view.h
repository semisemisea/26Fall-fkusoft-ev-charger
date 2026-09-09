/**
 * @file navigation_view.h
 * @brief 请求站点路线并在可用的 Qt WebEngine 环境中显示地图。
 */
#pragma once

#include "api/api_client.h"
#include "app/session.h"
#include "models/station.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWebEngineView;

/**
 * @brief 路线导航页：GET /locations/routes 后用 QWebEngineView 打开服务端返回的地图链接（密钥不出服务端）
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class NavigationView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入会话与 API 客户端，搭建导航页界面
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit NavigationView(Session &session, ApiClient &api, QWidget *parent = nullptr);

	/**
	 * @brief 以目标充电站打开本页，重置状态等待用户发起路线规划
	 * @param station 目标电站的界面模型。
	 */
	void open(const Station &station);

signals:
	/**
	 * @brief 用户点击返回
	 */
	void backRequested();

private:
	/**
	 * @brief 请求服务端规划路线（GET /locations/routes），成功后加载地图页面
	 */
	void requestRoute();

	Session &m_session;						 ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;						 ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	Station m_station;						 ///< 当前展示或导航的电站快照。
	QComboBox *m_modeCombo = nullptr;		 ///< 驾车或步行模式选择框。
	QPushButton *m_navigateButton = nullptr; ///< 开始导航或重新规划路线的按钮。
	QLabel *m_summaryLabel = nullptr;		 ///< 服务端路线距离和时长摘要标签。
	QLabel *m_statusLabel = nullptr;		 ///< 列表加载、空数据或错误状态标签。
	QLabel *m_hintLabel = nullptr;			 ///< 操作指引或当前状态提示标签。
	QVBoxLayout *m_layout = nullptr;		 ///< 承载路线摘要与惰性创建地图视图的布局。
	QWebEngineView *m_webView = nullptr;	 ///< 首次成功规划后创建的地图视图，由本页拥有；无 WebEngine 时保持空。
	bool m_loaded = false;					 ///< 是否已加载过地图，用于切换模式时自动重新规划。
};
