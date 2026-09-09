/**
 * @file combo_box.h
 * @brief 定制下拉框绘制、选项委托及弹层圆角遮罩。
 */
#pragma once

#include <QComboBox>

/**
 * @brief 自绘下拉框：自绘 chevron 与选中态，弹层圆角裁剪（见 DESIGN.md 组件规范）
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class ComboBox : public QComboBox {
	Q_OBJECT

public:
	/**
	 * @brief 创建下拉框及自绘选项委托，连接选择后关闭弹层的动作。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ComboBox(QWidget *parent = nullptr);

	/**
	 * @brief 加宽加高默认尺寸，给自绘 chevron 留位
	 * @return 建议布局尺寸，保留自绘箭头或选项内容所需空间。
	 */
	QSize sizeHint() const override;

	/**
	 * @brief 弹出时给弹层窗口加圆角遮罩
	 */
	void showPopup() override;

protected:
	/**
	 * @brief 自绘边框、文本与下拉箭头
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void paintEvent(QPaintEvent *event) override;
};
