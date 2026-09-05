#include "AppIcons.h"

#include "common/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

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
    painter.setBrush(theme::error());
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

QPixmap AppIcons::search(const QColor &color, int size, bool badge)
{
	QPixmap pixmap = makePixmap(size);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.scale(size / 24.0, size / 24.0);
	painter.setPen(QPen(color, 2.2));
	painter.setBrush(Qt::NoBrush);

		   // 画圆圈（放大镜的镜片）
	painter.drawEllipse(QPointF(9.5, 9.5), 6.0, 6.0);

		   // 画手柄（放大镜的柄）
	painter.setPen(QPen(color, 2.5));
	painter.drawLine(QPointF(14.0, 14.0), QPointF(20.5, 20.5));

	if (badge) {
		drawBadge(painter);
	}
	return pixmap;
}

QPixmap AppIcons::clock(const QColor &color, int size, bool badge)
{
	// SVG 模板（用 %1 占位颜色）
	QString svgTemplate = R"(
		<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
		<path d="M12 7V12H9M21 12C21 16.9706 16.9706 21 12 21C7.02944 21 3 16.9706 3 12C3 7.02944 7.02944 3 12 3C16.9706 3 21 7.02944 21 12Z" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
		</svg>
	)";

	QString colorHex = color.name();
	QString fullSvg = svgTemplate.arg(colorHex);

	QSvgRenderer renderer(fullSvg.toUtf8());
	QPixmap pixmap = makePixmap(size);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);

	if (badge) {
		drawBadge(painter);
	}
	return pixmap;
}
