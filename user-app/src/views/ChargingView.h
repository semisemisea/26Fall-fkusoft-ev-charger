/**
 * @file ChargingView.h
 * @brief 展示充电订单的真实计量与费用，管理订单轮询和停止充电交互。
 */
#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class ChargePollThread;
class ChargingSpinnerWidget;
class QLabel;
class QPushButton;

/**
 * @brief 充电进行页：ChargePollThread 每 5 秒轮询 GET /orders/{id}，刷新卡片与金额
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class ChargingView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 创建充电指标卡片、停止按钮和由页面拥有的轮询线程。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ChargingView(ApiClient &api, QWidget *parent = nullptr);
	/**
	 * @brief 加载传入数据并重置页面显示状态。
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void open(const Order &order);

signals:
	/**
	 * @brief 停止充电成功后发出服务端订单快照，由外层进入结算页。
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void orderStopped(const Order &order);

protected:
	/**
	 * @brief 处理控件隐藏事件并停止与可见性绑定的后台活动。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void hideEvent(QHideEvent *event) override;

private:
	/**
	 * @brief 直接用服务端订单电量、单价和金额刷新充电指标。
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void updateDisplay(const Order &order);
	/**
	 * @brief 确认后停止轮询并提交停止订单；请求失败时恢复轮询。
	 */
	void stopCharging();

	ApiClient &m_api;								  ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	Order m_order;									  ///< 当前页面持有的订单快照。
	QString m_chargerType;							  ///< 独立电桩详情请求返回的 fast/slow；尚未获取时为空。
	ChargePollThread *m_pollThread = nullptr;		  ///< 页面拥有的订单轮询线程；隐藏页面时请求停止并等待退出。
	ChargingSpinnerWidget *m_spinnerWidget = nullptr; ///< 页面拥有的充电装饰动画控件。
	QLabel *m_rangeValueLabel = nullptr;			  ///< 单价标签（分转元后显示每度价格）。
	QLabel *m_energyValueLabel = nullptr;			  ///< 累计充入电量标签，单位 kWh，不按电池容量截断。
	QLabel *m_typeValueLabel = nullptr;				  ///< 快充或慢充类型标签。
	QLabel *m_remainValueLabel = nullptr;			  ///< 充电桩编号标签。
	QLabel *m_feeValueLabel = nullptr;				  ///< 服务端预估费用标签。
	QPushButton *m_stopButton = nullptr;			  ///< 提交停止充电的按钮。
};
