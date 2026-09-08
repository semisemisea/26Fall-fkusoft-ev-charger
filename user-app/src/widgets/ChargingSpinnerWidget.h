/**
 * @file ChargingSpinnerWidget.h
 * @brief 绘制双层旋转圆角图形和浮动电池组成的充电动效。
 */
#pragma once

#include <QTimer>
#include <QWidget>

/**
 * @brief 双层旋转动效：两层 45% 圆角正方形错开旋转，边缘产生波浪效果
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class ChargingSpinnerWidget : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造双层旋转动画，启动更新旋转角度和浮动相位的定时器。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ChargingSpinnerWidget(QWidget *parent = nullptr);

protected:
	/**
	 * @brief 根据当前显示状态重绘控件。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;

private:
	double m_angle = 0.0;	   ///< 当前旋转角度，单位为度。
	double m_bobPhase = 0.0;   ///< 上下浮动相位，单位为度；绘制时转换为弧度。
	QTimer *m_timer = nullptr; ///< 本控件拥有的动画帧定时器。
};
