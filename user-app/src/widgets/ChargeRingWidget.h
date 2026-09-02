#pragma once

#include <QWidget>

class ChargeRingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChargeRingWidget(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int m_angle = 0;
};
