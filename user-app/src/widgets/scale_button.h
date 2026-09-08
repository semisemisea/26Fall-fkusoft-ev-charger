/**
 * @file scale_button.h
 * @brief 以 Qt 属性动画提供按钮按下、释放和移出的缩放反馈。
 */
#pragma once

#include <QEasingCurve>
#include <QPushButton>

class QPropertyAnimation;

/**
 * @brief 按压反馈按钮：按下时缩放（正常 0.97，EV_DEMO_SLOW 演示模式 0.90）
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class ScaleButton : public QPushButton {
	Q_OBJECT
	/// @brief QPropertyAnimation 驱动的绘制缩放比例，读写方法为 scale()/setScale()。
	Q_PROPERTY(qreal scale READ scale WRITE setScale)

public:
	/**
	 * @brief 创建带文字的按钮，并准备驱动 scale 属性的动画。
	 * @param text 需要显示的文字。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ScaleButton(const QString &text, QWidget *parent = nullptr);

	/**
	 * @brief 缩放动画属性（供 QPropertyAnimation 驱动）
	 * @return 当前缩放比例。
	 */
	[[nodiscard]] qreal scale() const { return m_scale; } ///< 绘制时使用的缩放比例，默认 1。
	/**
	 * @brief 写入当前缩放比例并请求重绘。
	 * @param scale 动画属性使用的缩放比例，1 表示原始大小。
	 */
	void setScale(qreal scale);

protected:
	/**
	 * @brief 按 m_scale 缩放自绘按钮
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;
	/**
	 * @brief 按下缩小、松开/移出回弹
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void mousePressEvent(QMouseEvent *event) override;
	/**
	 * @brief 处理鼠标释放事件并更新控件交互状态。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void mouseReleaseEvent(QMouseEvent *event) override;
	/**
	 * @brief 鼠标移出后恢复按钮的正常比例。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void leaveEvent(QEvent *event) override;

private:
	/**
	 * @brief 启动缩放动画到目标值
	 * @param target 本次动画结束时的缩放比例。
	 * @param duration 动画时长，单位为毫秒。
	 * @param curve 缩放动画使用的 Qt 缓动曲线。
	 */
	void animateTo(qreal target, int duration, QEasingCurve::Type curve);

	qreal m_scale = 1.0;				  ///< 绘制时使用的缩放比例，默认 1。
	QPropertyAnimation *m_anim = nullptr; ///< 本按钮拥有的缩放属性动画。
};
