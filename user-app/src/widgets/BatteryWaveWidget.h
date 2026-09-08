/**
 * @file BatteryWaveWidget.h
 * @brief 以裁剪渐变和正弦波模拟竖电池内部液面动画。
 */
#pragma once

#include <QTimer>
#include <QWidget>

/**
 * @brief 带波浪动效的竖电池图标：渐变填充高度随电量变化，上边沿有正弦波动画
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class BatteryWaveWidget : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造电池波浪控件并启动每帧更新相位的定时器。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit BatteryWaveWidget(QWidget *parent = nullptr);
	/**
	 * @brief 更新归一化电量比例并请求重绘。
	 * @param percent 归一化电量比例；限制在 0 到 1，显示文字时再乘 100。
	 */
	void setPercent(double percent);

protected:
	/**
	 * @brief 根据当前显示状态重绘控件。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;

private:
	double m_percent = 0.0;	   ///< 归一化电量比例，设置时限制在 0 到 1。
	double m_wavePhase = 0.0;  ///< 电池波浪的当前弧度相位。
	QTimer *m_timer = nullptr; ///< 本控件拥有的动画帧定时器。
};
