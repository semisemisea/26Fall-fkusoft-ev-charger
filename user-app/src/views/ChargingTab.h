#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Order.h"
#include "models/Reservation.h"

#include <QWidget>
#include <functional>

class ChargingView;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class SettleView;

// 充电 Tab 调度者：内嵌准备页 / ChargingView / SettleView；进入时查 /me/active-order 恢复现场
class ChargingTab : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入会话与 API 客户端，搭建准备页 / 预约页 / 充电页 / 结算页的堆栈
    explicit ChargingTab(Session &session, ApiClient &api, QWidget *parent = nullptr);

    // 查询 /me/active-order，按结果恢复到充电中 / 待结算 / 预约 / 准备页
    void checkActiveOrder();
    // 切换到充电进行页
    void showCharging(const Order &order);
    // 切换到结算页
    void showSettlement(const Order &order);
    // 切换到预约详情页并启动保留时长倒计时
    void showReservation(const Reservation &reservation);

signals:
    // 结算完成后发出，用于刷新余额等
    void orderSettled();
    // 用户请求返回首页
    void returnHomeRequested();
    // 是否存在进行中的订单或预约
    void activeOrderChanged(bool hasActiveOrder);

private:
    // 搭建“准备充电”页（输入电桩编号启动）
    void buildPreparePage();
    // 搭建“我的预约”页（倒计时 + 启动 / 取消预约）
    void buildReservationPage();
    // 切回准备页
    void showPrepare();
    // 以输入的电桩编号开始：查附近电桩并创建订单
    void startWithCode();
    // 获取附近充电站候选列表
    void fetchNearbyStations();
    // 逐个候选站点查找编号匹配的电桩
    void tryNextCandidate();
    // 为指定电桩创建充电订单（POST /orders）
    void createOrder(int chargerId);
    // 启动失败：提示错误并回到准备页
    void fail(const QString &message);
    // 查询是否有生效中的预约
    void checkActiveReservation();
    // 从预约直接创建订单启动充电
    void startFromReservation();
    // 取消当前预约
    void cancelReservation();
    // 每秒刷新预约保留时长倒计时
    void updateCountdown();
    // 预约页按钮置忙 / 恢复
    void setReservationBusy(bool busy);

    Session &m_session;
    ApiClient &m_api;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_preparePage = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLineEdit *m_codeEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    ChargingView *m_chargingView = nullptr;
    SettleView *m_settleView = nullptr;
    QString m_pendingCode;
    QVector<int> m_candidateStationIds;
    int m_candidateIndex = 0;

    QWidget *m_reservationPage = nullptr;
    QLabel *m_reservationStationLabel = nullptr;
    QLabel *m_reservationChargerLabel = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QLabel *m_reservationHintLabel = nullptr;
    QPushButton *m_reservationStartButton = nullptr;
    QPushButton *m_reservationCancelButton = nullptr;
    QTimer *m_countdownTimer = nullptr;
    Reservation m_reservation;
};
