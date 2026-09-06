#include "ChargeRingWidget.h"

#include "common/Theme.h"

#include "common/Demo.h"

#include <QHideEvent>
#include <QPainter>
#include <QShowEvent>
#include <QVariantAnimation>

// 固定 132x132 尺寸
ChargeRingWidget::ChargeRingWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(132, 132);
}

// 显示时启动无限循环的角度动画；时长经 demo::ms 适配演示模式
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

// 隐藏时停止并销毁全部动画，避免后台空转
void ChargeRingWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    const auto animations = findChildren<QVariantAnimation *>();
    for (QVariantAnimation *animation : animations) {
        animation->stop();
        animation->deleteLater();
    }
}

// 绘制灰色底盘环与 110 度进度弧；drawArc 角度单位为 1/16 度
void ChargeRingWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF rect = this->rect().adjusted(10, 10, -10, -10);
    QPen backgroundPen(theme::border(), 9, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(backgroundPen);
    painter.drawEllipse(rect);

    QPen progressPen(theme::successActive(), 9, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progressPen);
    painter.drawArc(rect, -m_angle * 16, -110 * 16);

    QFont font = this->font();
    font.setPixelSize(44);
    painter.setFont(font);
    painter.drawText(rect, Qt::AlignCenter, QStringLiteral("⚡"));
}
