/**
 * @file station_detail_view.h
 * @brief 展示站内电桩并将预约、充电和导航意图交给主窗口。
 */
#pragma once

#include "api/api_client.h"
#include "models/charger.h"
#include "models/station.h"

#include <QWidget>
#include <QPixmap>
#include <QVector>

class QLabel;
class QPushButton;
class QFrame;
class Spinner;
class QVBoxLayout;
class QResizeEvent;

/**
 * @brief 电站详情页：站内电桩列表（GET /stations/{id}/chargers）；只发意图信号，跳转由 MainWindow 编排
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class StationDetailView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：搭建详情页界面（背景图、信息标签、电桩列表、底部按钮）
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit StationDetailView(ApiClient &api, QWidget *parent = nullptr);

	/**
	 * @brief 打开指定电站：填充站点信息并加载电桩列表
	 * @param station 目标电站的界面模型。
	 */
	void open(const Station &station);

signals:
	/**
	 * @brief 请求返回上一页
	 */
	void backRequested();
	/**
	 * @brief 请求对选中电桩充电，由 MainWindow 下单
	 * @param charger 目标电桩的界面模型。
	 */
	void chargeRequested(const Charger &charger);
	/**
	 * @brief 请求预约选中电桩，由 MainWindow 提交预约
	 * @param charger 目标电桩的界面模型。
	 */
	void reservationRequested(const Charger &charger);
	/**
	 * @brief 请求导航到当前电站
	 * @param station 目标电站的界面模型。
	 */
	void navigateRequested(const Station &station);

private:
	/**
	 * @brief 加载站内电桩列表（GET /stations/{id}/chargers）并逐行渲染
	 */
	void loadChargers();
	/**
	 * @brief 根据选中电桩状态刷新底部预约/充电按钮的可用性与样式
	 */
	void updateBottomButtons();
	/**
	 * @brief 拦截电桩行的点击事件以实现单选高亮
	 * @param obj 当前接收事件的电桩行对象。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 * @return 事件已由本控件处理时为 true，否则沿用基类过滤结果。
	 */
	bool eventFilter(QObject *obj, QEvent *event) override;

	ApiClient &m_api;						 ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	Station m_station;						 ///< 当前展示或导航的电站快照。
	QPushButton *m_backButton = nullptr;	 ///< 返回上一页面的按钮。
	QPushButton *m_navButton = nullptr;		 ///< 请求导航到当前站点的按钮。
	QLabel *m_nameLabel = nullptr;			 ///< 站点名称标签。
	QLabel *m_infoLabel = nullptr;			 ///< 站点地址等补充信息标签。
	QLabel *m_distanceLabel = nullptr;		 ///< 站点距离标签；卡片中同时作为导航点击区域。
	QLabel *m_priceLabel = nullptr;			 ///< 站点或订单单价标签。
	QLabel *m_availabilityLabel = nullptr;	 ///< 站内空闲电桩数量标签。
	QLabel *m_statusLabel = nullptr;		 ///< 列表加载、空数据或错误状态标签。
	Spinner *m_spinner = nullptr;			 ///< 由页面拥有的加载动画控件。
	QVBoxLayout *m_chargersLayout = nullptr; ///< 动态电桩行列表布局。
	QPixmap m_bgPixmap;						 ///< 从资源加载的背景图像值。
	QWidget *m_bgSpacer = nullptr;			 ///< 为顶部按比例绘制的背景预留布局高度。

	QPushButton *m_reserveButton = nullptr; ///< 对选中电桩发出预约意图的按钮。
	QPushButton *m_chargeButton = nullptr;	///< 对选中电桩发出充电意图的按钮。

	QVector<Charger> m_chargers;	 ///< 最近一次站内电桩请求返回的快照列表。
	Charger m_selectedCharger;		 ///< 当前选中电桩的值副本。
	bool m_hasSelection = false;	 ///< 当前是否存在可供底部按钮使用的选中电桩。
	QFrame *m_selectedRow = nullptr; ///< 当前高亮的电桩行，行控件由页面容器拥有。

protected:
	/**
	 * @brief 绘制顶部背景图及下方渐变填充
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;
	/**
	 * @brief 窗口尺寸变化时按宽度等比调整背景占位高度
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void resizeEvent(QResizeEvent *event) override;
};
