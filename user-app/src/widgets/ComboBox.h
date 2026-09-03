#pragma once

#include <QComboBox>

class ComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ComboBox(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    void showPopup() override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
