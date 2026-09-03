#include "Spinner.h"

#include "common/Theme.h"

#include "common/Demo.h"

#include <QPainter>
#include <QTimer>

Spinner::Spinner(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(20, 20);
    m_timer = new QTimer(this);
    m_timer->setInterval(demo::ms(16));
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 12) % 360;
        update();
    });
}

void Spinner::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer->start();
}

void Spinner::hideEvent(QHideEvent *event)
{
    m_timer->stop();
    QWidget::hideEvent(event);
}

void Spinner::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF ring = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPen background(theme::border(), 2.4, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(background);
    painter.drawEllipse(ring);

    QPen progress(theme::primary(), 2.4, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progress);
    painter.drawArc(ring, -m_angle * 16, -280 * 16);
}
