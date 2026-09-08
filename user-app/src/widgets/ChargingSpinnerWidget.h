#pragma once

#include <QTimer>
#include <QWidget>

// 双层旋转动效：两层 45% 圆角正方形错开旋转，边缘产生波浪效果
class ChargingSpinnerWidget : public QWidget {
	Q_OBJECT

public:
	explicit ChargingSpinnerWidget(QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	double m_angle = 0.0;	 // 当前旋转角度（度）
	double m_bobPhase = 0.0; // 上下浮动相位
	QTimer *m_timer = nullptr;
};
