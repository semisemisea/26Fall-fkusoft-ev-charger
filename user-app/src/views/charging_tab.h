/**
 * @file charging_tab.h
 * @brief 协调准备、预约、充电和结算页面，并从服务端恢复当前业务状态。
 */
#pragma once

#include "api/api_client.h"
#include "app/session.h"
#include "models/order.h"
#include "models/reservation.h"

#include <QWidget>
#include <functional>

class ChargingView;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class SettleView;

/**
 * @brief 充电 Tab 调度者：内嵌准备页 / ChargingView / SettleView；进入时查 /me/active-order 恢复现场
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class ChargingTab : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入会话与 API 客户端，搭建准备页 / 预约页 / 充电页 / 结算页的堆栈
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ChargingTab(Session &session, ApiClient &api, QWidget *parent = nullptr);

	/**
	 * @brief 查询 /me/active-order，按结果恢复到充电中 / 待结算 / 预约 / 准备页
	 */
	void checkActiveOrder();
	/**
	 * @brief 切换到充电进行页
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void showCharging(const Order &order);
	/**
	 * @brief 切换到结算页
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void showSettlement(const Order &order);
	/**
	 * @brief 切换到预约详情页并启动保留时长倒计时
	 * @param reservation 包含服务端 expiresAt 的预约快照。
	 */
	void showReservation(const Reservation &reservation);

signals:
	/**
	 * @brief 结算完成后发出，用于刷新余额等
	 */
	void orderSettled();
	/**
	 * @brief 用户请求返回首页
	 */
	void returnHomeRequested();
	/**
	 * @brief 是否存在进行中的订单或预约
	 * @param hasActiveOrder 是否存在需要用户继续处理的订单或预约。
	 */
	void activeOrderChanged(bool hasActiveOrder);

private:
	/**
	 * @brief 搭建“准备充电”页（输入电桩编号启动）
	 */
	void buildPreparePage();
	/**
	 * @brief 搭建“我的预约”页（倒计时 + 启动 / 取消预约）
	 */
	void buildReservationPage();
	/**
	 * @brief 切回准备页
	 */
	void showPrepare();
	/**
	 * @brief 以输入的电桩编号开始：查附近电桩并创建订单
	 */
	void startWithCode();
	/**
	 * @brief 获取附近充电站候选列表
	 */
	void fetchNearbyStations();
	/**
	 * @brief 逐个候选站点查找编号匹配的电桩
	 */
	void tryNextCandidate();
	/**
	 * @brief 为指定电桩创建充电订单（POST /orders）
	 * @param chargerId 要创建充电订单的电桩标识。
	 */
	void createOrder(int chargerId);
	/**
	 * @brief 启动失败：提示错误并回到准备页
	 * @param message 展示给用户的失败原因。
	 */
	void fail(const QString &message);
	/**
	 * @brief 查询是否有生效中的预约
	 */
	void checkActiveReservation();
	/**
	 * @brief 从预约直接创建订单启动充电
	 */
	void startFromReservation();
	/**
	 * @brief 取消当前预约
	 */
	void cancelReservation();
	/**
	 * @brief 每秒刷新预约保留时长倒计时
	 */
	void updateCountdown();
	/**
	 * @brief 预约页按钮置忙 / 恢复
	 * @param busy true 禁用预约操作按钮，false 恢复按钮。
	 */
	void setReservationBusy(bool busy);

	Session &m_session;						///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;						///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QStackedWidget *m_stack = nullptr;		///< 充电流程内部页面堆栈，负责拥有加入其中的子页面。
	QWidget *m_preparePage = nullptr;		///< 无活动业务时显示的准备页。
	QLabel *m_hintLabel = nullptr;			///< 操作指引或当前状态提示标签。
	QLineEdit *m_codeEdit = nullptr;		///< 直接启动充电所用的电桩编号输入框。
	QPushButton *m_startButton = nullptr;	///< 按输入编号启动充电的按钮。
	ChargingView *m_chargingView = nullptr; ///< 充电进行页，由页面堆栈管理。
	SettleView *m_settleView = nullptr;		///< 订单结算页，由页面堆栈管理。
	QString m_pendingCode;					///< 本轮异步查找使用的输入编号快照。
	QVector<int> m_candidateStationIds;		///< 附近站点候选标识，供逐站查询电桩。
	int m_candidateIndex = 0;				///< 下一次需要检查的候选站点索引。

	QWidget *m_reservationPage = nullptr;			  ///< 展示当前生效预约的子页面。
	QLabel *m_reservationStationLabel = nullptr;	  ///< 预约所属站点名称标签。
	QLabel *m_reservationChargerLabel = nullptr;	  ///< 预约电桩编号标签。
	QLabel *m_countdownLabel = nullptr;				  ///< 预约剩余时间的 mm:ss 标签。
	QLabel *m_reservationHintLabel = nullptr;		  ///< 预约操作失败或状态说明标签。
	QPushButton *m_reservationStartButton = nullptr;  ///< 使用当前预约启动订单的按钮。
	QPushButton *m_reservationCancelButton = nullptr; ///< 取消当前预约的按钮。
	QTimer *m_countdownTimer = nullptr;				  ///< 页面拥有的每秒倒计时定时器；切换业务状态时显式启停。
	Reservation m_reservation;						  ///< 当前预约快照，倒计时以 expiresAt 为准。
};
