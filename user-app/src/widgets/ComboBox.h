#pragma once

#include <QComboBox>

// 自绘下拉框：自绘 chevron 与选中态，弹层圆角裁剪（见 DESIGN.md 组件规范）
class ComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ComboBox(QWidget *parent = nullptr);

    // 加宽加高默认尺寸，给自绘 chevron 留位
    QSize sizeHint() const override;

    // 弹出时给弹层窗口加圆角遮罩
    void showPopup() override;

protected:
    // 自绘边框、文本与下拉箭头
    void paintEvent(QPaintEvent *event) override;
};
