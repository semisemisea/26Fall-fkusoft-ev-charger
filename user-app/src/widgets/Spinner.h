#pragma once

#include <QWidget>

class QTimer;

// 加载指示器：自绘旋转圆弧，显示时启动计时、隐藏即停
class Spinner : public QWidget
{
    Q_OBJECT

public:
    explicit Spinner(QWidget *parent = nullptr);

protected:
    // 自绘底盘圆环 + 旋转圆弧
    void paintEvent(QPaintEvent *event) override;
    // 显示/隐藏时启停旋转计时器
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    int m_angle = 0;
    QTimer *m_timer = nullptr;
};
