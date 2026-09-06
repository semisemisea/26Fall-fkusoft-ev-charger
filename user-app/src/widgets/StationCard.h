#pragma once

#include "models/Station.h"

#include <QFrame>

class QLabel;

// 电站卡片：列表中的单个电站条目（名称/地址/单价/空闲数/距离），点击发 clicked 信号
class StationCard : public QFrame
{
    Q_OBJECT

public:
    explicit StationCard(const Station &station, QWidget *parent = nullptr);

    // 卡片对应的电站数据
    [[nodiscard]] const Station &station() const { return m_station; }
    // 搜索过滤：名称或地址包含关键字（不区分大小写）
    [[nodiscard]] bool matches(const QString &filter) const;

signals:
    // 点击卡片主体 / 请求导航
    void clicked(const Station &station);
    void navigateRequested(const Station &station);

protected:
    // 卡片范围内松开鼠标视为点击
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 拦截距离标签的点击，转发为导航请求
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Station m_station;
    QLabel *m_distanceLabel = nullptr;
};
