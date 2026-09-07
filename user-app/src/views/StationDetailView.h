#pragma once

#include "api/ApiClient.h"
#include "models/Charger.h"
#include "models/Station.h"

#include <QWidget>
#include <QPixmap>
#include <QVector>

class QLabel;
class QPushButton;
class QFrame;
class Spinner;
class QVBoxLayout;
class QResizeEvent;

// 电站详情页：站内电桩列表（GET /stations/{id}/chargers）；只发意图信号，跳转由 MainWindow 编排
class StationDetailView : public QWidget
{
    Q_OBJECT

public:
    // 构造函数：搭建详情页界面（背景图、信息标签、电桩列表、底部按钮）
    explicit StationDetailView(ApiClient &api, QWidget *parent = nullptr);

    // 打开指定电站：填充站点信息并加载电桩列表
    void open(const Station &station);

signals:
    // 请求返回上一页
    void backRequested();
    // 请求对选中电桩充电，由 MainWindow 下单
    void chargeRequested(const Charger &charger);
    // 请求预约选中电桩，由 MainWindow 提交预约
    void reservationRequested(const Charger &charger);
    // 请求导航到当前电站
    void navigateRequested(const Station &station);

private:
    // 加载站内电桩列表（GET /stations/{id}/chargers）并逐行渲染
    void loadChargers();
    // 根据选中电桩状态刷新底部预约/充电按钮的可用性与样式
    void updateBottomButtons();
    // 拦截电桩行的点击事件以实现单选高亮
    bool eventFilter(QObject *obj, QEvent *event) override;

    ApiClient &m_api;
    Station m_station;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_navButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_distanceLabel = nullptr;
    QLabel *m_priceLabel = nullptr;
    QLabel *m_availabilityLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_chargersLayout = nullptr;
    QPixmap m_bgPixmap;
    QWidget *m_bgSpacer = nullptr;

    QPushButton *m_reserveButton = nullptr;
    QPushButton *m_chargeButton = nullptr;

    QVector<Charger> m_chargers;
    Charger m_selectedCharger;
    bool m_hasSelection = false;
    QFrame *m_selectedRow = nullptr;

protected:
    // 绘制顶部背景图及下方渐变填充
    void paintEvent(QPaintEvent *event) override;
    // 窗口尺寸变化时按宽度等比调整背景占位高度
    void resizeEvent(QResizeEvent *event) override;
};