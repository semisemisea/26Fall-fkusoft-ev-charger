#include "StationCard.h"

#include "common/Format.h"

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace {
const QLatin1String kCardStyle{
    "StationCard { background: white; border: 1px solid #ddd; border-radius: 10px; }"
    "StationCard:hover { border-color: #2a6fdb; }"};
}

StationCard::StationCard(const Station &station, QWidget *parent)
    : QFrame(parent)
    , m_station(station)
{
    setStyleSheet(kCardStyle);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(84);

    auto *nameLabel = new QLabel(station.name, this);
    nameLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; border: none;"));

    auto *priceLabel = new QLabel(QStringLiteral("%1 元/度").arg(fenToYuan(station.pricePerKwhFen)), this);
    priceLabel->setStyleSheet(QStringLiteral("color: #2a6fdb; font-weight: bold; border: none;"));

    auto *addressLabel = new QLabel(station.address, this);
    addressLabel->setStyleSheet(QStringLiteral("color: #777; border: none;"));

    auto *idleLabel = new QLabel(QStringLiteral("空闲 %1/%2").arg(station.availableChargerCount).arg(station.chargerCount), this);
    idleLabel->setStyleSheet(station.availableChargerCount > 0
                                 ? QStringLiteral("color: #2e9e5b; border: none;")
                                 : QStringLiteral("color: #d33; border: none;"));

    auto *distanceLabel = new QLabel(QStringLiteral("%1 km").arg(station.distanceKm, 0, 'f', 2), this);
    distanceLabel->setStyleSheet(QStringLiteral("color: #2a6fdb; text-decoration: underline; border: none;"));
    distanceLabel->setCursor(Qt::PointingHandCursor);
    distanceLabel->setToolTip(QStringLiteral("点击导航"));
    distanceLabel->installEventFilter(this);
    m_distanceLabel = distanceLabel;

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(nameLabel);
    topRow->addStretch();
    topRow->addWidget(priceLabel);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(idleLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(distanceLabel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->addLayout(topRow);
    layout->addWidget(addressLabel);
    layout->addLayout(bottomRow);
}

void StationCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (rect().contains(event->pos())) {
        emit clicked(m_station);
    }
    QFrame::mouseReleaseEvent(event);
}

bool StationCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_distanceLabel && event->type() == QEvent::MouseButtonRelease) {
        emit navigateRequested(m_station);
        return true;
    }
    return QFrame::eventFilter(watched, event);
}
