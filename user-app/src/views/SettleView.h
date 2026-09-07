#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"

#include <QWidget>

class QLabel;
class QPushButton;

// 结算页：展示服务端算定的账单并钱包扣款；余额不足（INSUFFICIENT_BALANCE）时引导充值后再结算
class SettleView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入会话与 API 客户端，搭建账单与支付按钮界面
    explicit SettleView(Session &session, ApiClient &api, QWidget *parent = nullptr);

    // 打开待结算订单：填充账单明细并恢复初始按钮状态
    void open(const Order &order);

signals:
    // 支付成功
    void settled();
    // 用户选择稍后支付
    void dismissed();
    // 用户请求返回首页
    void returnHomeRequested();

private:
    // 调用 POST /orders/{id}/settle 用钱包余额支付
    void settle();
    // 余额不足时打开充值对话框，充值成功后恢复支付入口
    void openRecharge();
    // 刷新页面上显示的钱包余额
    void refreshBalance();
    // 将 ISO 时间串裁剪为 "yyyy-MM-dd hh:mm" 显示格式
    static QString formatTime(QString isoTime);

    Session &m_session;
    ApiClient &m_api;
    Order m_order;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_stationLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_energyLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_priceLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QPushButton *m_payButton = nullptr;
    QPushButton *m_topUpButton = nullptr;
    QPushButton *m_laterButton = nullptr;
    QPushButton *m_homeButton = nullptr;
    // 结算页容器 / 支付成功页容器
    QWidget *m_settleContainer = nullptr;
    QWidget *m_successContainer = nullptr;
    QLabel *m_successIconLabel = nullptr;
    QLabel *m_successAmountLabel = nullptr;
    QLabel *m_successTitleLabel = nullptr;
    QLabel *m_successSubtitleLabel = nullptr;
    QPushButton *m_successHomeButton = nullptr;
};
