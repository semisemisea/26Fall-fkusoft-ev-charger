#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Station.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWebEngineView;

// 路线导航页：GET /locations/routes 后用 QWebEngineView 打开服务端返回的地图链接（密钥不出服务端）
class NavigationView : public QWidget
{
    Q_OBJECT

public:
    // 构造：注入会话与 API 客户端，搭建导航页界面
    explicit NavigationView(Session &session, ApiClient &api, QWidget *parent = nullptr);

    // 以目标充电站打开本页，重置状态等待用户发起路线规划
    void open(const Station &station);

signals:
    // 用户点击返回
    void backRequested();

private:
    // 请求服务端规划路线（GET /locations/routes），成功后加载地图页面
    void requestRoute();

    Session &m_session;
    ApiClient &m_api;
    Station m_station;
    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_navigateButton = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWebEngineView *m_webView = nullptr;
    bool m_loaded = false;
};
