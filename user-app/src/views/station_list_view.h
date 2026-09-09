/**
 * @file station_list_view.h
 * @brief 查询附近电站，支持地图选址、手动坐标、文本过滤和基于空闲率的本地推荐。
 */
#pragma once

#include "api/api_client.h"
#include "app/session.h"
#include "models/station.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class Spinner;
class QVBoxLayout;
class StationCard;

/**
 * @brief 找桩页：附近电站列表（GET /stations/nearby，按距离排序）、定位切换、搜索与 AI 推荐横幅
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class StationListView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：搭建定位/搜索/列表/推荐横幅界面
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit StationListView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 用户点击某电站卡片（含 AI 推荐横幅），请求打开详情页
	 * @param station 目标电站的界面模型。
	 */
	void stationSelected(const Station &station);
	/**
	 * @brief 用户请求导航到某电站，由 MainWindow 打开导航页
	 * @param station 目标电站的界面模型。
	 */
	void navigateRequested(const Station &station);

protected:
	/**
	 * @brief 页面每次显示时刷新附近电站列表
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/**
	 * @brief 重新加载附近电站（GET /stations/nearby）并重建卡片列表
	 */
	void reload();
	/// @brief 打开腾讯地图选址，选点只回填输入框，确认后才查询。
	void pickLocation();
	/// @brief 校验并使用手动填写的经纬度。
	void searchCoordinates();
	/// @brief 更新查询及导航共用的位置，并刷新附近电站。
	void useLocation(double latitude, double longitude);
	/// @brief 清空旧结果，开始新请求并使旧回调失效。
	quint64 beginSearch();
	/**
	 * @brief 按搜索框关键字过滤卡片可见性
	 */
	void applyFilter();
	/**
	 * @brief 按当前空闲率生成本地推荐
	 */
	void loadRecommendation();

	Session &m_session;					   ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;					   ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLineEdit *m_latitudeEdit = nullptr;   ///< 手动纬度输入。
	QLineEdit *m_longitudeEdit = nullptr;  ///< 手动经度输入。
	QLabel *m_locationLabel = nullptr;	   ///< 实际查询中心。
	quint64 m_requestGeneration = 0;	   ///< 只接受最新请求的结果。
	QLineEdit *m_searchEdit = nullptr;	   ///< 按站名或地址过滤的输入框。
	QPushButton *m_bannerButton = nullptr; ///< 显示本地推荐站点并打开详情的横幅按钮。
	QLabel *m_statusLabel = nullptr;	   ///< 列表加载、空数据或错误状态标签。
	Spinner *m_spinner = nullptr;		   ///< 由页面拥有的加载动画控件。
	QScrollArea *m_scrollArea = nullptr;   ///< 拥有电站卡片容器的滚动区域。
	QVBoxLayout *m_cardsLayout = nullptr;  ///< 电站卡片布局。
	QVector<StationCard *> m_cards;		   ///< 卡片指针索引；实际对象由 Qt 父子树拥有。
	Station m_recommendedStation;		   ///< 本轮按空闲率选择的推荐站点快照。
	bool m_hasRecommendation = false;	   ///< 是否存在可供横幅打开的推荐站点。
	bool m_locationConfirmed = false;	   ///< 首次确认位置后才允许加载列表。
	bool m_listAnimated = false;		   ///< 是否已执行首轮列表淡入，避免刷新时重复播放。
};
