#pragma once

#include <QEasingCurve>
#include <QPushButton>

class QPropertyAnimation;

class ScaleButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale)

public:
    explicit ScaleButton(const QString &text, QWidget *parent = nullptr);

    [[nodiscard]] qreal scale() const { return m_scale; }
    void setScale(qreal scale);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void animateTo(qreal target, int duration, QEasingCurve::Type curve);

    qreal m_scale = 1.0;
    QPropertyAnimation *m_anim = nullptr;
};
