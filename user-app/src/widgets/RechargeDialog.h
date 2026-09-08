/**
 * @file RechargeDialog.h
 * @brief 校验充值金额并提交模拟钱包充值，成功后广播最新余额。
 */
#pragma once

#include "api/ApiClient.h"

#include <QDialog>
#include <QtGlobal>

class QLineEdit;
class QPushButton;
class QPropertyAnimation;

/**
 * @brief 充值弹窗：POST /me/wallet/topups 模拟充值（立即成功），成功发 succeeded(新余额)
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class RechargeDialog : public QDialog {
	Q_OBJECT

public:
	/**
	 * @brief 创建金额输入、支付按钮和对话框入场动画。
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit RechargeDialog(ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 充值成功，携带最新余额（分）
	 * @param balanceFen 最新钱包余额，单位为分。
	 */
	void succeeded(qlonglong balanceFen);

protected:
	/**
	 * @brief 首次显示时淡入
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/**
	 * @brief 校验金额并发起充值请求
	 */
	void pay();

	ApiClient &m_api;						  ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLineEdit *m_amountEdit = nullptr;		  ///< 以元为单位输入充值金额的编辑框。
	QPushButton *m_payButton = nullptr;		  ///< 发起支付或充值的按钮，请求期间禁用。
	QPropertyAnimation *m_fadeAnim = nullptr; ///< 对话框拥有的透明度入场动画。
};
