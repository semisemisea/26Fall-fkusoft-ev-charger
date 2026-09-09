/**
 * @file scale_button.cpp
 * @brief 以 Qt 属性动画提供按钮按下、释放和移出的缩放反馈。
 */
#include "scale_button.h"

#include "common/demo.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOptionButton>

/**
 * @details 创建驱动 scale 属性的动画对象
 */
ScaleButton::ScaleButton(const QString &text, QWidget *parent)
	: QPushButton(text, parent) {
	m_anim = new QPropertyAnimation(this, "scale", this);
}

/**
 * @details 记录缩放值并触发重绘
 */
void ScaleButton::setScale(qreal scale) {
	m_scale = scale;
	update();
}

/**
 * @details 打断进行中的动画，从当前值平滑过渡到目标值
 */
void ScaleButton::animateTo(qreal target, int duration, QEasingCurve::Type curve) {
	m_anim->stop();
	m_anim->setDuration(duration);
	m_anim->setStartValue(m_scale);
	m_anim->setEndValue(target);
	m_anim->setEasingCurve(curve);
	m_anim->start();
}

/**
 * @details 左键按下时缩小（幅度由演示模式决定）
 */
void ScaleButton::mousePressEvent(QMouseEvent *event) {
	QPushButton::mousePressEvent(event);
	if (event->button() == Qt::LeftButton) {
		animateTo(demo::pressScale(), demo::ms(90), QEasingCurve::OutCubic);
	}
}

/**
 * @details 松开回弹到原尺寸
 */
void ScaleButton::mouseReleaseEvent(QMouseEvent *event) {
	QPushButton::mouseReleaseEvent(event);
	animateTo(1.0, demo::ms(140), QEasingCurve::OutQuint);
}

/**
 * @details 指针移出时若未按住也回弹，避免卡在缩小态
 */
void ScaleButton::leaveEvent(QEvent *event) {
	QPushButton::leaveEvent(event);
	if (!isDown()) {
		animateTo(1.0, demo::ms(140), QEasingCurve::OutQuint);
	}
}

/**
 * @details 缩放为 1 时走默认绘制；否则以中心为原点缩放后交给 QStyle 绘制
 */
void ScaleButton::paintEvent(QPaintEvent *event) {
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
