#pragma once

#include <QPushButton>

// 覆盖页统一返回按钮
class BackButton : public QPushButton
{
    Q_OBJECT

public:
    // 构造 36x36 圆形返回按钮，自带左向箭头图标
    explicit BackButton(QWidget *parent = nullptr);
};
