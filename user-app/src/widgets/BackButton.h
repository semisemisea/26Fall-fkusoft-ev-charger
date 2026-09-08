/**
 * @file BackButton.h
 * @brief 提供覆盖页面共用的圆形返回按钮。
 */
#pragma once

#include <QPushButton>

/**
 * @brief 覆盖页统一返回按钮
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class BackButton : public QPushButton {
	Q_OBJECT

public:
	/**
	 * @brief 构造 36x36 圆形返回按钮，自带左向箭头图标
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit BackButton(QWidget *parent = nullptr);
};
