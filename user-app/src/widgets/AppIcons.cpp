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

QPixmap AppIcons::calendar(const QColor &color, int size, bool badge)
{
	// SVG 模板（用 %1 占位颜色）
	QString svgTemplate = R"(
		<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
		<path d="M10 21H6.2C5.0799 21 4.51984 21 4.09202 20.782C3.71569 20.5903 3.40973 20.2843 3.21799 19.908C3 19.4802 3 18.9201 3 17.8V8.2C3 7.0799 3 6.51984 3.21799 6.09202C3.40973 5.71569 3.71569 5.40973 4.09202 5.21799C4.51984 5 5.0799 5 6.2 5H17.8C18.9201 5 19.4802 5 19.908 5.21799C20.2843 5.40973 20.5903 5.71569 20.782 6.09202C21 6.51984 21 7.0799 21 8.2V10M7 3V5M17 3V5M3 9H21M13.5 13.0001L7 13M10 17.0001L7 17M14 21L16.025 20.595C16.2015 20.5597 16.2898 20.542 16.3721 20.5097C16.4452 20.4811 16.5147 20.4439 16.579 20.399C16.6516 20.3484 16.7152 20.2848 16.8426 20.1574L21 16C21.5523 15.4477 21.5523 14.5523 21 14C20.4477 13.4477 19.5523 13.4477 19 14L14.8426 18.1574C14.7152 18.2848 14.6516 18.3484 14.601 18.421C14.5561 18.4853 14.5189 18.5548 14.4903 18.6279C14.458 18.7102 14.4403 18.7985 14.405 18.975L14 21Z" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
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

QPixmap AppIcons::wallet(const QColor &color, int size, bool badge)
{
	// SVG 模板（用 %1 占位颜色）
	QString svgTemplate = R"(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
		<g id="money_bag_3" data-name="money bag 3">
		<path d="M17.67,22.5H6.33A4.83,4.83,0,0,1,1.5,17.67h0a4.83,4.83,0,0,1,1.24-3.23l7.35-8.17h3.82l7.35,8.17a4.83,4.83,0,0,1,1.24,3.23h0A4.83,4.83,0,0,1,17.67,22.5Z" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<path d="M15.82,1.5l-.39,2A3.49,3.49,0,0,1,12,6.27h0A3.49,3.49,0,0,1,8.57,3.46l-.39-2Z" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<line x1="18.68" y1="3.41" x2="14.86" y2="5.32" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<line x1="19.64" y1="6.27" x2="13.91" y2="6.27" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<path d="M10.09,17.73h2.39a1.43,1.43,0,0,0,1.43-1.43h0a1.43,1.43,0,0,0-1.43-1.44h-1a1.43,1.43,0,0,1-1.43-1.43h0A1.43,1.43,0,0,1,11.52,12h2.39" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<line x1="12" y1="11.05" x2="12" y2="12" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		<line x1="12" y1="17.73" x2="12" y2="18.68" stroke="%1" stroke-width="1.91" fill="none" stroke-linecap="square" stroke-miterlimit="10"/>
		</g>
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
