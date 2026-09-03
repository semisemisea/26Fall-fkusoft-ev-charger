#include "StationCard.h"

#include "common/Format.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

StationCard::StationCard(const Station &station, QWidget *parent)
    : QFrame(parent)
    , m_station(station)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(96);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(18);
    shadow->setColor(QColor(0, 0, 0, 35));
    shadow->setOffset(0, 3);
    setGraphicsEffect(shadow);

    auto *nameLabel = new QLabel(station.name, this);
    nameLabel->setObjectName(QStringLiteral("cardTitle"));

    m_distanceLabel = new QLabel(QStringLiteral("%1 km").arg(station.distanceKm, 0, 'f', 2), this);
    m_distanceLabel->setObjectName(QStringLiteral("linkBlue"));
    m_distanceLabel->setCursor(Qt::PointingHandCursor);
    m_distanceLabel->setToolTip(QStringLiteral("点击导航"));
    m_distanceLabel->installEventFilter(this);

    auto *navButton = new QPushButton(QStringLiteral("↱"), this);
    navButton->setObjectName(QStringLiteral("navMini"));
    navButton->setCursor(Qt::PointingHandCursor);
    navButton->setToolTip(QStringLiteral("一键导航"));
    connect(navButton, &QPushButton::clicked, this, [this] { emit navigateRequested(m_station); });

    auto *distanceRow = new QHBoxLayout;
    distanceRow->setSpacing(6);
    distanceRow->addWidget(m_distanceLabel);
    distanceRow->addWidget(navButton);

    auto *addressLabel = new QLabel(station.address, this);
    addressLabel->setObjectName(QStringLiteral("meta"));

    auto *priceLabel = new QLabel(QStringLiteral("￥%1/度").arg(fenToYuan(station.pricePerKwhFen)), this);
    priceLabel->setObjectName(QStringLiteral("price"));

    auto *idleLabel = new QLabel(QStringLiteral("空闲 %1 / %2")
                                     .arg(station.availableChargerCount)
                                     .arg(station.chargerCount),
                                 this);
    idleLabel->setObjectName(QStringLiteral("strong"));
    idleLabel->setStyleSheet(station.availableChargerCount > 0 ? QStringLiteral("color: #2e9e5b;")
                                                               : QStringLiteral("color: #e74c3c;"));

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(nameLabel, 0, 0);
    grid->addLayout(distanceRow, 0, 1, Qt::AlignRight);
    grid->addWidget(addressLabel, 1, 0, 1, 2);
    grid->addWidget(priceLabel, 2, 0);
    grid->addWidget(idleLabel, 2, 1, Qt::AlignRight);
    grid->setColumnStretch(0, 1);
}

bool StationCard::matches(const QString &filter) const
{
    if (filter.isEmpty()) {
        return true;
    }
    return m_station.name.contains(filter, Qt::CaseInsensitive)
        || m_station.address.contains(filter, Qt::CaseInsensitive);
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
