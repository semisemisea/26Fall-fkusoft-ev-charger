#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"

#include <QWidget>

class QLabel;

// 个人中心：资料/头像/余额维护，以及订单、预约、流水等历史记录入口
class ProfileView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入会话与 API 客户端，搭建资料卡 / 钱包卡 / 菜单列表
    explicit ProfileView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
    // 以下信号为各菜单入口跳转请求：历史订单 / 预约记录 / 钱包流水 / 我的爱车 / 关于系统
    void ordersRequested();
    void reservationsRequested();
    void transactionsRequested();
    void carRequested();
    void aboutRequested();

protected:
    // 页面显示时刷新资料与余额
    void showEvent(QShowEvent *event) override;
    // 拦截头像标签的点击事件以触发更换头像
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 用会话中的用户信息刷新昵称、手机号、余额与头像
    void refreshProfile();
    // 按当前 avatarUrl 下载并显示头像，失败时回退默认图标
    void loadAvatar();
    // 选择本地图片并上传为新头像（POST /me/avatar）
    void changeAvatar();
    // 弹窗输入新昵称并提交 PATCH /me
    void changeNickname();
    // 打开充值对话框
    void openRecharge();
    // 确认后退出登录
    void signOut();

    Session &m_session;
    ApiClient &m_api;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nicknameLabel = nullptr;
    QLabel *m_phoneLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QString m_loadedAvatarUrl;
};
