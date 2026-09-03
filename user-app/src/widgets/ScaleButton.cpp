#include "ScaleButton.h"

#include "common/Demo.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOptionButton>

ScaleButton::ScaleButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    m_anim = new QPropertyAnimation(this, "scale", this);
}

void ScaleButton::setScale(qreal scale)
{
    m_scale = scale;
    update();
}

void ScaleButton::animateTo(qreal target, int duration, QEasingCurve::Type curve)
{
    m_anim->stop();
    m_anim->setDuration(duration);
    m_anim->setStartValue(m_scale);
    m_anim->setEndValue(target);
    m_anim->setEasingCurve(curve);
    m_anim->start();
}

void ScaleButton::mousePressEvent(QMouseEvent *event)
{
    QPushButton::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        animateTo(demo::pressScale(), demo::ms(90), QEasingCurve::OutCubic);
    }
}

void ScaleButton::mouseReleaseEvent(QMouseEvent *event)
{
    QPushButton::mouseReleaseEvent(event);
    animateTo(1.0, demo::ms(140), QEasingCurve::OutQuint);
}

void ScaleButton::leaveEvent(QEvent *event)
{
    QPushButton::leaveEvent(event);
    if (!isDown()) {
        animateTo(1.0, demo::ms(140), QEasingCurve::OutQuint);
    }
}

void ScaleButton::paintEvent(QPaintEvent *event)
{
    if (qFuzzyCompare(m_scale, 1.0)) {
        QPushButton::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(m_scale, m_scale);
    painter.translate(-width() / 2.0, -height() / 2.0);

    QStyleOptionButton option;
    initStyleOption(&option);
    style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
}
