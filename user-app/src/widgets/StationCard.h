#pragma once

#include "models/Station.h"

#include <QFrame>

class QLabel;

class StationCard : public QFrame
{
    Q_OBJECT

public:
    explicit StationCard(const Station &station, QWidget *parent = nullptr);

signals:
    void clicked(const Station &station);
    void navigateRequested(const Station &station);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Station m_station;
    QLabel *m_distanceLabel = nullptr;
};
