#pragma once

#include <QWidget>

class QTimer;

class Spinner : public QWidget
{
    Q_OBJECT

public:
    explicit Spinner(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    int m_angle = 0;
    QTimer *m_timer = nullptr;
};
