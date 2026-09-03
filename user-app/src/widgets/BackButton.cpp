#include "BackButton.h"

#include "common/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {
QIcon makeChevronIcon()
{
    const QSize size(24, 24);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(theme::textPrimary(), 2.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    QPainterPath path;
    path.moveTo(15, 4);
    path.lineTo(8, 12);
    path.lineTo(15, 20);
    painter.drawPath(path);

    return QIcon(pixmap);
}
} // namespace

BackButton::BackButton(QWidget *parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("roundBackButton"));
    setFixedSize(36, 36);
    setIcon(makeChevronIcon());
    setIconSize(QSize(20, 20));
    setCursor(Qt::PointingHandCursor);
}
