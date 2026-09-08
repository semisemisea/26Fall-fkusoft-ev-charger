/**
 * @file Spinner.h
 * @brief 提供显示时运行、隐藏时停止的圆弧加载指示器。
 */
#pragma once

#include <QWidget>

class QTimer;

/**
 * @brief 加载指示器：自绘旋转圆弧，显示时启动计时、隐藏即停
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class Spinner : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造加载指示器和帧定时器；定时器由显示事件启动。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit Spinner(QWidget *parent = nullptr);

protected:
	/**
	 * @brief 自绘底盘圆环 + 旋转圆弧
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;
	/**
	 * @brief 显示/隐藏时启停旋转计时器
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;
	/**
	 * @brief 处理控件隐藏事件并停止与可见性绑定的后台活动。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void hideEvent(QHideEvent *event) override;

private:
	int m_angle = 0;		   ///< 当前旋转角度，单位为度。
	QTimer *m_timer = nullptr; ///< 本控件拥有的动画帧定时器。
};
