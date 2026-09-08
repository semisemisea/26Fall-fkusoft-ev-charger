#pragma once

#include <QWidget>
#include <QTimer>

// 带波浪动效的竖电池图标：渐变填充高度随电量变化，上边沿有正弦波动画
class BatteryWaveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BatteryWaveWidget(QWidget *parent = nullptr);
    void setPercent(double percent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_percent = 0.0;
    double m_wavePhase = 0.0;
    QTimer *m_timer = nullptr;
};