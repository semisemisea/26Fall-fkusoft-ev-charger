#include "AppIcons.h"

#include <QPainter>
#include <QPainterPath>

namespace {
QPixmap makePixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

void drawBadge(QPainter &painter)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QPointF(18.8, 4.8), 4.2, 4.2);
    painter.setBrush(QColor(0xe7, 0x4c, 0x3c));
    painter.drawEllipse(QPointF(18.8, 4.8), 3.0, 3.0);
}
} // namespace

QPixmap AppIcons::pin(const QColor &color, int size, bool badge)
{
    QPixmap pixmap = makePixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPainterPath head;
    head.addEllipse(QPointF(12, 9.8), 6.6, 6.6);
    QPainterPath tail;
    tail.moveTo(7.3, 14.4);
    tail.lineTo(12, 21.8);
    tail.lineTo(16.7, 14.4);
    tail.closeSubpath();
    painter.drawPath(head.united(tail));

    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.drawEllipse(QPointF(12, 9.8), 2.6, 2.6);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (badge) {
        drawBadge(painter);
    }
    return pixmap;
}

QPixmap AppIcons::bolt(const QColor &color, int size, bool badge)
{
    QPixmap pixmap = makePixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPainterPath path;
    path.moveTo(13.5, 2.5);
    path.lineTo(5.5, 13.5);
    path.lineTo(10.5, 13.5);
    path.lineTo(9.0, 21.5);
    path.lineTo(18.5, 9.5);
    path.lineTo(13.0, 9.5);
    path.closeSubpath();
    painter.drawPath(path);

    if (badge) {
        drawBadge(painter);
    }
    return pixmap;
}

QPixmap AppIcons::person(const QColor &color, int size, bool badge)
{
    QPixmap pixmap = makePixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    painter.drawEllipse(QPointF(12, 8.2), 4.2, 4.2);

    QPainterPath body;
    body.moveTo(3.5, 21.8);
    body.cubicTo(3.5, 15.5, 7.3, 13.8, 12, 13.8);
    body.cubicTo(16.7, 13.8, 20.5, 15.5, 20.5, 21.8);
    body.closeSubpath();
    painter.drawPath(body);

    if (badge) {
        drawBadge(painter);
    }
    return pixmap;
}
