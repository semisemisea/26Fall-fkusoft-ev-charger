#pragma once

#include <QWidget>

// 充电环：充电页的环形进度动画，随轮询数据刷新
class ChargeRingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChargeRingWidget(QWidget *parent = nullptr);

protected:
    // 显示/隐藏时启停旋转动画（动效只服务状态变化，不可见即停）
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    // 自绘底盘圆环 + 旋转进度弧 + 中心闪电字符
    void paintEvent(QPaintEvent *event) override;

private:
    int m_angle = 0;
};
