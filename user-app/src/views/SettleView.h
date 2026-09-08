/**
 * @file SettleView.h
 * @brief 展示服务端账单并发起钱包结算，余额不足时引导充值。
 */
#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"

#include <QWidget>

class QLabel;
class QPushButton;

/**
 * @brief 结算页：展示服务端算定的账单并钱包扣款；余额不足（INSUFFICIENT_BALANCE）时引导充值后再结算
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class SettleView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入会话与 API 客户端，搭建账单与支付按钮界面
	 * @param session 共享会话的非拥有引用，必须比当前页面存活更久。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit SettleView(Session &session, ApiClient &api, QWidget *parent = nullptr);

	/**
	 * @brief 打开待结算订单：填充账单明细并恢复初始按钮状态
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void open(const Order &order);

signals:
	/**
	 * @brief 支付成功
	 */
	void settled();
	/**
	 * @brief 用户选择稍后支付
	 */
	void dismissed();
	/**
	 * @brief 用户请求返回首页
	 */
	void returnHomeRequested();

private:
	/**
	 * @brief 调用 POST /orders/{id}/settle 用钱包余额支付
	 */
	void settle();
	/**
	 * @brief 余额不足时打开充值对话框，充值成功后恢复支付入口
	 */
	void openRecharge();
	/**
	 * @brief 刷新页面上显示的钱包余额
	 */
	void refreshBalance();
	/**
	 * @brief 将 ISO 时间串裁剪为 "yyyy-MM-dd hh:mm" 显示格式
	 * @param isoTime 待显示的 ISO 时间文本；仅裁剪和替换分隔符，不做时区转换。
	 * @return 适合账单显示的日期和分钟文本。
	 */
	static QString formatTime(QString isoTime);

	Session &m_session;					  ///< 共享会话；页面保存非拥有引用，主窗口保存由自身拥有的对象指针。
	ApiClient &m_api;					  ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	Order m_order;						  ///< 当前页面持有的订单快照。
	QLabel *m_titleLabel = nullptr;		  ///< 当前账单页面标题。
	QLabel *m_stationLabel = nullptr;	  ///< 账单所属站点标签。
	QLabel *m_timeLabel = nullptr;		  ///< 时间显示标签；主窗口显示时钟，结算页显示充电时段。
	QLabel *m_energyLabel = nullptr;	  ///< 账单累计电量标签，单位 kWh。
	QLabel *m_durationLabel = nullptr;	  ///< 充电持续分钟数标签。
	QLabel *m_priceLabel = nullptr;		  ///< 站点或订单单价标签。
	QLabel *m_totalLabel = nullptr;		  ///< 服务端账单总金额标签。
	QLabel *m_balanceLabel = nullptr;	  ///< 钱包余额标签，显示时由分转换为元。
	QLabel *m_messageLabel = nullptr;	  ///< 请求失败或操作提示标签。
	QPushButton *m_payButton = nullptr;	  ///< 发起支付或充值的按钮，请求期间禁用。
	QPushButton *m_topUpButton = nullptr; ///< 余额不足时显示的充值入口。
	QPushButton *m_laterButton = nullptr; ///< 暂不结算并离开当前账单的按钮。
	QPushButton *m_homeButton = nullptr;  ///< 结算界面返回首页按钮。
	// 结算页容器 / 支付成功页容器
	QWidget *m_settleContainer = nullptr;		///< 账单与支付操作容器。
	QWidget *m_successContainer = nullptr;		///< 结算成功状态容器。
	QLabel *m_successIconLabel = nullptr;		///< 支付成功图标标签。
	QLabel *m_successAmountLabel = nullptr;		///< 成功页已支付金额标签。
	QLabel *m_successTitleLabel = nullptr;		///< 成功页主标题。
	QLabel *m_successSubtitleLabel = nullptr;	///< 成功页补充说明。
	QPushButton *m_successHomeButton = nullptr; ///< 成功页返回首页按钮。
};
