/**
 * @file charging_spinner_widget.cpp
 * @brief 绘制双层旋转圆角图形和浮动电池组成的充电动效。
 */
#include "charging_spinner_widget.h"

#include "widgets/app_icons.h"

#include <QImage>
#include <QPainter>
#include <QRadialGradient>
#include <cmath>

/**
 * @details 创建 30 ms 动画定时器；旋转与上下浮动均约十秒一周期，随控件销毁释放。
 */
ChargingSpinnerWidget::ChargingSpinnerWidget(QWidget *parent)
	: QWidget(parent) {
	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, [this] {
		// 10 秒旋转一圈：每 30ms 增加 360/(10000/30) ≈ 1.08 度
		m_angle += 1.08;
		if (m_angle >= 360.0) {
			m_angle -= 360.0;
		}
		// 上下浮动相位同步
		m_bobPhase += 1.08;
		if (m_bobPhase >= 360.0) {
			m_bobPhase -= 360.0;
		}
		update();
	});
	m_timer->start(30);
}

/**
 * @details 以较短边确定尺寸，先绘制偏移背景层再绘制正面层和电池，不改变控件布局几何。
 */
void ChargingSpinnerWidget::paintEvent(QPaintEvent *event) {
	Q_UNUSED(event)
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const double side = qMin(width(), height()) * 0.72;
	const double half = side / 2.0;

	// 上下浮动：幅度为边长的 2%，周期与旋转同步
	const double bobY = -side * 0.02 * std::sin(m_bobPhase * M_PI / 180.0);

	painter.translate(width() / 2.0, height() / 2.0 + bobY);

	// 四个后层：47% 圆角，荧光绿半透明，分别向下、左、右、上偏移 4%（先绘制）
	const double off = side * 0.04;
	const QPointF backOffsets[4] = {
		QPointF(0, off), QPointF(-off, 0), QPointF(off, 0), QPointF(0, -off)};
	const int backAlphas[4] = {60, 80, 100, 120};
	for (int i = 0; i < 4; ++i) {
		painter.save();
		painter.translate(backOffsets[i]);
		painter.rotate(m_angle);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0x2B, 0xFF, 0x7D, backAlphas[i]));
		const double radiusBack = side * 0.47;
		painter.drawRoundedRect(QRectF(-half, -half, side, side), radiusBack, radiusBack);
		painter.restore();
	}

	// 前层（绿）：45% 圆角，中心黄绿 → 边缘荧光绿径向渐变，同相位同步旋转
	painter.save();
	painter.rotate(m_angle);
	painter.setPen(Qt::NoPen);
	QRadialGradient frontGrad(0, 0, half * 1.45);
	frontGrad.setColorAt(0.0, QColor(0xFF, 0xF2, 0x00, 200));
	frontGrad.setColorAt(1.0, QColor(0x2B, 0xFF, 0x7D, 200));
	painter.setBrush(frontGrad);
	const double radiusFront = side * 0.45;
	painter.drawRoundedRect(QRectF(-half, -half, side, side), radiusFront, radiusFront);
	painter.restore();

	// 前层中心绘制黄白色闪电（固定竖直方向，边缘径向柔化）
	const double boltSize = side * 0.40;
	QImage boltImg = AppIcons::bolt(QColor("#FFF9D6"), qRound(boltSize)).toImage().convertToFormat(QImage::Format_ARGB32);
	const double bcx = (boltImg.width() - 1) / 2.0;
	const double bcy = (boltImg.height() - 1) / 2.0;
	const double bmaxD = std::sqrt(bcx * bcx + bcy * bcy);
	for (int y = 0; y < boltImg.height(); ++y) {
		for (int x = 0; x < boltImg.width(); ++x) {
			const double dx = (x - bcx) / bmaxD;
			const double dy = (y - bcy) / bmaxD;
			const double d2 = dx * dx + dy * dy;
			const double fade = qBound(0.0, 1.0 - d2, 1.0);
			QColor c = boltImg.pixelColor(x, y);
			c.setAlphaF(c.alphaF() * fade);
			// 中心提亮：离中心越近越亮
			c.setRed(qMin(255, c.red() + qRound(20 * fade)));
			c.setGreen(qMin(255, c.green() + qRound(20 * fade)));
			c.setBlue(qMin(255, c.blue() + qRound(20 * fade)));
			boltImg.setPixelColor(x, y, c);
		}
	}
	const QPixmap boltPix = QPixmap::fromImage(boltImg);
	const QRect boltRect(qRound(-boltSize / 2.0), qRound(-boltSize / 2.0), qRound(boltSize), qRound(boltSize));
	painter.drawPixmap(boltRect, boltPix);
}
