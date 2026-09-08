#pragma once

#include "api/ApiClient.h"

#include <QWidget>

class QLabel;
class Spinner;
class QVBoxLayout;

// 历史与信息页合集：订单/流水/预约记录 + 车辆/关于，结构相同——进入时 load() 拉取列表

// 历史订单（GET /orders）
class OrderHistoryView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入 API 客户端并搭建带返回键的列表页骨架
    explicit OrderHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
    // 用户点击返回
    void backRequested();

protected:
    // 页面显示时触发列表加载
    void showEvent(QShowEvent *event) override;

private:
    // 拉取历史订单列表并渲染卡片
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

// 钱包流水（GET /me/wallet/transactions）
class TransactionsView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入 API 客户端并搭建流水列表页骨架
    explicit TransactionsView(ApiClient &api, QWidget *parent = nullptr);

signals:
    // 用户点击返回
    void backRequested();

protected:
    // 页面显示时触发流水加载
    void showEvent(QShowEvent *event) override;

private:
    // 拉取钱包流水并渲染卡片（充值 / 扣款 / 退款 / 调整）
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

// 预约记录（GET /reservations）
class ReservationHistoryView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入 API 客户端并搭建预约记录列表页骨架
    explicit ReservationHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
    // 用户点击返回
    void backRequested();

protected:
    // 页面显示时触发预约记录加载
    void showEvent(QShowEvent *event) override;

private:
    // 拉取预约记录并渲染卡片
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

// 关于系统页（版本与简介）
class AboutView : public QWidget
{
    Q_OBJECT

public:
    // 构造：搭建关于页
    explicit AboutView(QWidget *parent = nullptr);

signals:
    // 用户点击返回
    void backRequested();
};
