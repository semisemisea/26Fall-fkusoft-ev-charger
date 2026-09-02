#include "StationListView.h"

#include "common/Format.h"
#include "widgets/StationCard.h"

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {
struct LocationPreset
{
    const char *name;
    double latitude;
    double longitude;
};

const LocationPreset kLocationPresets[] = {
    {"大连市中心", 38.914, 121.614},
    {"软件园", 38.889, 121.537},
    {"星海广场", 38.881, 121.584},
    {"东港商务区", 38.928, 121.663},
    {"大连北站", 39.056, 121.585},
};
}

StationListView::StationListView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    auto *title = new QLabel(QStringLiteral("附近充电站"), this);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));

    m_locationCombo = new QComboBox(this);
    for (const LocationPreset &preset : kLocationPresets) {
        m_locationCombo->addItem(QStringLiteral("📍 %1").arg(preset.name));
    }
    connect(m_locationCombo, &QComboBox::activated, this, [this] { reload(); });

    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    connect(refreshButton, &QPushButton::clicked, this, [this] { reload(); });

    m_welcomeLabel = new QLabel(this);
    m_welcomeLabel->setStyleSheet(QStringLiteral("color: #555;"));

    auto *activeOrderButton = new QPushButton(QStringLiteral("进行中订单"), this);
    connect(activeOrderButton, &QPushButton::clicked, this, &StationListView::checkActiveOrderRequested);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(m_locationCombo);
    headerRow->addWidget(refreshButton);

    auto *userRow = new QHBoxLayout;
    userRow->addWidget(m_welcomeLabel);
    userRow->addStretch();
    userRow->addWidget(activeOrderButton);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #d33;"));
    m_statusLabel->hide();

    auto *cardsContainer = new QWidget(this);
    m_cardsLayout = new QVBoxLayout(cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(cardsContainer);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(headerRow);
    layout->addLayout(userRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(scrollArea);

    refreshWelcome();
    connect(&m_session, &Session::userChanged, this, &StationListView::refreshWelcome);
}

void StationListView::refreshWelcome()
{
    m_welcomeLabel->setText(QStringLiteral("%1 · 余额 %2 元")
                                .arg(m_session.user().nickname,
                                     fenToYuan(m_session.user().walletBalanceFen)));
}

void StationListView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reload();
}

void StationListView::reload()
{
    const LocationPreset &preset = kLocationPresets[m_locationCombo->currentIndex()];

    QUrlQuery query;
    query.addQueryItem(QLatin1String("latitude"), QString::number(preset.latitude));
    query.addQueryItem(QLatin1String("longitude"), QString::number(preset.longitude));

    m_statusLabel->setText(QStringLiteral("加载中..."));
    m_statusLabel->show();

    m_api.get(QStringLiteral("/stations/nearby?%1").arg(query.toString(QUrl::FullyEncoded)),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_statusLabel->hide();
                  while (m_cardsLayout->count() > 1) {
                      QLayoutItem *item = m_cardsLayout->takeAt(0);
                      item->widget()->deleteLater();
                      delete item;
                  }
                  const QJsonArray stations = data.toArray();
                  for (const QJsonValue &value : stations) {
                      auto *card = new StationCard(Station::fromJson(value.toObject()), this);
                      connect(card, &StationCard::clicked, this, &StationListView::stationSelected);
                      m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
                  }
                  if (stations.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("附近没有可用充电站"));
                      m_statusLabel->show();
                  }
              },
              [this](const ApiError &error) {
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}
