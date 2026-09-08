/**
 * @file charge_ring_widget.h
 * @brief 绘制循环旋转圆环，并随可见性管理动画生命周期。
 */
#pragma once

#include <QWidget>

/**
 * @brief 循环旋转的装饰圆环；无计量输入，不表示实际电量进度
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class ChargeRingWidget : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造固定为 132×132 像素的圆环控件。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ChargeRingWidget(QWidget *parent = nullptr);

protected:
	/**
	 * @brief 显示/隐藏时启停旋转动画（动效只服务状态变化，不可见即停）
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;
	/**
	 * @brief 处理控件隐藏事件并停止与可见性绑定的后台活动。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void hideEvent(QHideEvent *event) override;
	/**
	 * @brief 自绘底盘圆环 + 旋转进度弧 + 中心闪电字符
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;

private:
	int m_angle = 0; ///< 当前旋转角度，单位为度。
};
