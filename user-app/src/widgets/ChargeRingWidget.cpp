#include "ChargeRingWidget.h"

#include "common/Theme.h"

#include "common/Demo.h"

#include <QHideEvent>
#include <QPainter>
#include <QShowEvent>
#include <QVariantAnimation>

ChargeRingWidget::ChargeRingWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(132, 132);
}

void ChargeRingWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    auto *animation = new QVariantAnimation(this);
    animation->setStartValue(0);
    animation->setEndValue(360);
    animation->setDuration(demo::ms(1600));
    animation->setLoopCount(-1);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_angle = value.toInt();
        update();
    });
    connect(animation, &QVariantAnimation::destroyed, this, [this] { m_angle = 0; });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChargeRingWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    const auto animations = findChildren<QVariantAnimation *>();
    for (QVariantAnimation *animation : animations) {
        animation->stop();
        animation->deleteLater();
    }
}

void ChargeRingWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF rect = this->rect().adjusted(10, 10, -10, -10);
    QPen backgroundPen(theme::border(), 9, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(backgroundPen);
    painter.drawEllipse(rect);

    QPen progressPen(theme::success(), 9, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progressPen);
    painter.drawArc(rect, -m_angle * 16, -110 * 16);

    QFont font = this->font();
    font.setPixelSize(44);
    painter.setFont(font);
    painter.drawText(rect, Qt::AlignCenter, QStringLiteral("⚡"));
}
