#pragma once

#include "models/Station.h"

#include <QFrame>

class StationCard : public QFrame
{
    Q_OBJECT

public:
    explicit StationCard(const Station &station, QWidget *parent = nullptr);

signals:
    void clicked(const Station &station);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Station m_station;
};
