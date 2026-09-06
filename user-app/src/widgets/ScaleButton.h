#pragma once

#include <QEasingCurve>
#include <QPushButton>

class QPropertyAnimation;

// 按压反馈按钮：按下时缩放（正常 0.97，EV_DEMO_SLOW 演示模式 0.90）
class ScaleButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale)

public:
    explicit ScaleButton(const QString &text, QWidget *parent = nullptr);

    // 缩放动画属性（供 QPropertyAnimation 驱动）
    [[nodiscard]] qreal scale() const { return m_scale; }
    void setScale(qreal scale);

protected:
    // 按 m_scale 缩放自绘按钮
    void paintEvent(QPaintEvent *event) override;
    // 按下缩小、松开/移出回弹
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // 启动缩放动画到目标值
    void animateTo(qreal target, int duration, QEasingCurve::Type curve);

    qreal m_scale = 1.0;
    QPropertyAnimation *m_anim = nullptr;
};
