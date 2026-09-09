/**
 * @file app_icons.cpp
 * @brief 使用 QPainter 生成不依赖外部字体的矢量风格位图图标。
 */
#include "app_icons.h"

#include "common/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <cmath>

namespace {
	// 创建透明底色的正方形画布
	/**
	 * @brief 创建供自绘图标使用的透明方形画布。
	 * @param size 画布边长，单位像素。
	 * @return 已填充透明色的 QPixmap 值。
	 */
	QPixmap makePixmap(int size) {
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		return pixmap;
	}

	// 右上角红点角标：白底外圈 + 错误色实心圆
	/**
	 * @brief 在 24×24 基准坐标系中叠加红点角标。
	 * @param painter 调用方提供的画笔；本函数按固定基准坐标画红点，不自行适配输出尺寸。
	 */
	void drawBadge(QPainter &painter) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(Qt::white);
		painter.drawEllipse(QPointF(18.8, 4.8), 4.2, 4.2);
		painter.setBrush(theme::error());
		painter.drawEllipse(QPointF(18.8, 4.8), 3.0, 3.0);
	}
} // namespace

/**
 * @details 定位图钉：QPainterPath 拼出圆形头部 + 三角尾部，中心镂空
 */
QPixmap AppIcons::pin(const QColor &color, int size, bool badge) {
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

/**
 * @details 闪电：SVG 路径渲染（填充与描边同色）
 */
QPixmap AppIcons::bolt(const QColor &color, int size, bool badge) {
	const QString hex = color.name();
	const QString svg = QStringLiteral(
							"<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\" fill=\"none\">"
							"<path fill=\"%1\" stroke=\"%1\" stroke-linecap=\"round\" stroke-linejoin=\"round\" "
							"stroke-width=\"2\" d=\"M4 14 14 3v7h6L10 21v-7H4z\"/></svg>")
							.arg(hex);
	QSvgRenderer renderer(svg.toUtf8());
	QPixmap pixmap = makePixmap(size);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);
	if (badge) {
		drawBadge(painter);
	}
	return pixmap;
}

/**
 * @details 人形：圆头 + 贝塞尔曲线肩部
 */
QPixmap AppIcons::person(const QColor &color, int size, bool badge) {
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

/**
 * @details 放大镜：描边圆圈 + 斜线手柄
 */
QPixmap AppIcons::search(const QColor &color, int size, bool badge) {
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

/**
 * @details 信号强度：四根递增竖条
 */
QPixmap AppIcons::signal(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none">
		<rect x="2" y="16" width="3.5" height="6" rx="1" fill="%1"/>
		<rect x="7.5" y="12" width="3.5" height="10" rx="1" fill="%1"/>
		<rect x="13" y="8" width="3.5" height="14" rx="1" fill="%1"/>
		<rect x="18.5" y="4" width="3.5" height="18" rx="1" fill="%1"/>
		</svg>
	)SVG";

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

/**
 * @details 时钟：内嵌 SVG 模板替换颜色后由 QSvgRenderer 渲染
 */
QPixmap AppIcons::clock(const QColor &color, int size, bool badge) {
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

/**
 * @details 日历：SVG 模板渲染
 */
QPixmap AppIcons::calendar(const QColor &color, int size, bool badge) {
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

/**
 * @details 钱袋：SVG 模板渲染
 */
QPixmap AppIcons::wallet(const QColor &color, int size, bool badge) {
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

/**
 * @details 车辆：SVG 模板渲染
 */
QPixmap AppIcons::car(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
		<path d="M3 8L5.72187 10.2682C5.90158 10.418 6.12811 10.5 6.36205 10.5H17.6379C17.8719 10.5 18.0984 10.418 18.2781 10.2682L21 8M6.5 14H6.51M17.5 14H17.51M8.16065 4.5H15.8394C16.5571 4.5 17.2198 4.88457 17.5758 5.50772L20.473 10.5777C20.8183 11.1821 21 11.8661 21 12.5623V18.5C21 19.0523 20.5523 19.5 20 19.5H19C18.4477 19.5 18 19.0523 18 18.5V17.5H6V18.5C6 19.0523 5.55228 19.5 5 19.5H4C3.44772 19.5 3 19.0523 3 18.5V12.5623C3 11.8661 3.18166 11.1821 3.52703 10.5777L6.42416 5.50772C6.78024 4.88457 7.44293 4.5 8.16065 4.5ZM7 14C7 14.2761 6.77614 14.5 6.5 14.5C6.22386 14.5 6 14.2761 6 14C6 13.7239 6.22386 13.5 6.5 13.5C6.77614 13.5 7 13.7239 7 14ZM18 14C18 14.2761 17.7761 14.5 17.5 14.5C17.2239 14.5 17 14.2761 17 14C17 13.7239 17.2239 13.5 17.5 13.5C17.7761 13.5 18 13.7239 18 14Z" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
		</svg>
	)SVG";

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

/**
 * @details 信息图标：SVG 模板渲染
 */
QPixmap AppIcons::info(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
		<circle cx="12" cy="12" r="10" stroke="%1" stroke-width="1.5"/>
		<path d="M12 17V11" stroke="%1" stroke-width="1.5" stroke-linecap="round"/>
		<circle cx="1" cy="1" r="1" transform="matrix(1 0 0 -1 11 9)" fill="%1"/>
		</svg>
	)SVG";

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

/**
 * @details 右向尖括号：SVG 模板渲染
 */
QPixmap AppIcons::chevronRight(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
		<path d="M9 6L15 12L9 18" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
		</svg>
	)SVG";

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

/**
 * @details 右转箭头：SVG 模板渲染
 */
QPixmap AppIcons::turnRight(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none">
		<path d="M3.71493213,19.3875 C3.71493213,13.0983574 9.05986213,9 15.6531674,9 L19,9" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
		<polyline points="15 4 20 9 15 14 15 14" stroke="%1" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
		</svg>
	)SVG";

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

/**
 * @details 绘制电池图标（边框黑色，内部绿色填充）
 */
QPixmap AppIcons::battery(int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
		<rect x="2" y="6" width="18" height="14" rx="2" stroke="#000000" stroke-width="1.5" fill="none"/>
		<rect x="20" y="10" width="2" height="6" rx="0.5" fill="#000000"/>
		<rect x="4" y="8" width="14" height="10" rx="1" fill="#2BFF7D"/>
		</svg>
	)SVG";

	QSvgRenderer renderer(svgTemplate.toUtf8());
	QPixmap pixmap = makePixmap(size);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);

	if (badge) {
		drawBadge(painter);
	}
	return pixmap;
}

/**
 * @details 将归一化电量映射到渐变液面高度；输出为按电池纵横比生成的矩形，百分比文字保持底部对齐。
 */
QPixmap AppIcons::batteryVertical(int size, double percent, double wavePhase) {
	const double pct = qBound(0.0, percent, 1.0);

	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 17.10 34" xmlns="http://www.w3.org/2000/svg">
		<rect x="5.80" y="1.5" width="5.5" height="2" rx="1" fill="#000000"/>
		<rect x="1.5" y="3.5" width="14.10" height="29.5" rx="1.5" stroke="#000000" stroke-width="0.5" fill="#000000"/>
		</svg>
	)SVG";

	QSvgRenderer renderer(svgTemplate.toUtf8());
	const int width = size * 17.10 / 32;
	const int height = size * 34 / 32;
	QPixmap pixmap(width, height);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);

	// 绘制波浪上边沿的渐变填充
	const double scale = size / 32.0;
	const double fillX = 1.75 * scale;
	const double fillW = 13.60 * scale;
	const double fillBaseY = (3.75 + 29.0 * (1.0 - pct)) * scale;
	const double fillBottomY = (3.75 + 29.0) * scale;
	const double amplitude = 0.5 * scale;
	const int segments = 24;

	QPainterPath wavePath;
	wavePath.moveTo(fillX, fillBottomY);
	wavePath.lineTo(fillX, fillBaseY + amplitude * std::sin(wavePhase));
	for (int i = 1; i <= segments; ++i) {
		const double x = fillX + (fillW * i) / segments;
		const double y = fillBaseY + amplitude * std::sin(wavePhase + (i * 2.0 * M_PI * 2.0) / segments);
		wavePath.lineTo(x, y);
	}
	wavePath.lineTo(fillX + fillW, fillBottomY);
	wavePath.closeSubpath();

	QLinearGradient grad(fillX, fillBottomY, fillX, fillBaseY);
	grad.setColorAt(0.0, QColor("#FFF200"));
	grad.setColorAt(1.0, QColor("#2BFF7D"));
	painter.fillPath(wavePath, grad);

	// 绘制百分比文字（白色，居中于电池主体区域）
	const QString text = QString::number(pct * 100, 'f', 0) + "%";
	QFont font = painter.font();
	font.setPixelSize(28);
	painter.setFont(font);
	painter.setPen(QColor("#FFFFFF"));
	const int topPad = size * 3.5 / 32;
	const int bodyH = size * 29.5 / 32;
	const int bottomMargin = size * 4 / 32;
	const QRect textRect(0, topPad, width, bodyH - bottomMargin);
	painter.drawText(textRect, Qt::AlignBottom | Qt::AlignHCenter, text);

	return pixmap;
}
/**
 * @details 绘制用户头像图标（面部轮廓）
 */
QPixmap AppIcons::avatar(const QColor &color, int size, bool badge) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
		<path d="M0,15.832c0-4.947,6-3.958,6-5.937a2.881,2.881,0,0,0-.546-1.979A4.532,4.532,0,0,1,4,4.453,4.245,4.245,0,0,1,8,0a4.245,4.245,0,0,1,4,4.453,4.458,4.458,0,0,1-1.474,3.463A3,3,0,0,0,10,9.895c0,1.979,6,.989,6,5.937,0,0-1.593,1.168-8,1.168S0,15.832,0,15.832Z" transform="translate(4 3)" stroke="%1" stroke-width="1.5" fill="none" stroke-miterlimit="10"/>
		</svg>
	)SVG";

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

/**
 * @details 支付成功图标：渐变圆圈（主色→黄绿色）+ 主色打勾，背景透明
 */
QPixmap AppIcons::successCheck(int width, int height) {
	QString svgTemplate = R"SVG(
		<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
		<defs>
		<linearGradient id="circleGrad" x1="0%" y1="0%" x2="0%" y2="100%">
		<stop offset="0%" style="stop-color:%1;stop-opacity:1" />
		<stop offset="100%" style="stop-color:%2;stop-opacity:1" />
		</linearGradient>
		</defs>
		<path d="M12,22 C6.477,22 2,17.523 2,12 C2,6.477 6.477,2 12,2 C17.523,2 22,6.477 22,12 C22,17.523 17.523,22 12,22 Z M12,21.4 C16.857,21.4 21.4,16.857 21.4,12 C21.4,7.143 16.857,2.6 12,2.6 C7.143,2.6 2.6,7.143 2.6,12 C2.6,16.857 7.143,21.4 12,21.4 Z" fill="url(#circleGrad)" fill-rule="evenodd"/>
		<path d="M7.5 12.5 L10.8 15.8 L16.5 9.5" stroke="%3" stroke-width="0.6" stroke-linecap="round" stroke-linejoin="round" fill="none"/>
		</svg>
	)SVG";

	QString fullSvg = svgTemplate
						  .arg(theme::primary().name())
						  .arg(QStringLiteral("#FFF200"))
						  .arg(theme::primary().name());

	QSvgRenderer renderer(fullSvg.toUtf8());
	QPixmap pixmap(width, height);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	// 保持 SVG 宽高比（24:24），居中渲染，避免圆圈被拉伸为椭圆
	QRectF viewBox = renderer.viewBoxF();
	qreal svgAspect = viewBox.width() / viewBox.height();
	qreal pixAspect = qreal(width) / qreal(height);
	QRectF target;
	if (pixAspect > svgAspect) {
		qreal targetWidth = height * svgAspect;
		target = QRectF((width - targetWidth) / 2, 0, targetWidth, height);
	} else {
		qreal targetHeight = width / svgAspect;
		target = QRectF(0, (height - targetHeight) / 2, width, targetHeight);
	}
	renderer.render(&painter, target);
	return pixmap;
}
