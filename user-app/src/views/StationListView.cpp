#include "StationListView.h"

#include "widgets/StationCard.h"

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
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
    m_locationCombo = new QComboBox(this);
    for (const LocationPreset &preset : kLocationPresets) {
        m_locationCombo->addItem(QStringLiteral("📍 %1").arg(preset.name));
    }
    connect(m_locationCombo, &QComboBox::activated, this, [this] {
        const LocationPreset &preset = kLocationPresets[m_locationCombo->currentIndex()];
        m_session.setLocation(preset.latitude, preset.longitude);
        reload();
    });

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("🔍 搜索电站 / 地址"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    m_bannerButton = new QPushButton(this);
    m_bannerButton->setObjectName(QStringLiteral("aiBanner"));
    m_bannerButton->setCursor(Qt::PointingHandCursor);
    m_bannerButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        " stop:0 #eef3ff, stop:1 #f6f0ff); color: #1d5cff;"
        " border: 1px solid #d8e2ff; border-radius: 10px; padding: 9px 12px; text-align: left; }"));
    m_bannerButton->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #d33;"));
    m_statusLabel->hide();

    auto *cardsContainer = new QWidget(this);
    m_cardsLayout = new QVBoxLayout(cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(10);
    m_cardsLayout->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(cardsContainer);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(m_locationCombo);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_bannerButton);
    layout->addWidget(m_statusLabel);
    layout->addWidget(scrollArea);

    connect(m_bannerButton, &QPushButton::clicked, this, [this] {
        if (m_hasRecommendation) {
            emit stationSelected(m_recommendedStation);
        }
    });
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
    m_bannerButton->hide();
    m_hasRecommendation = false;

    m_api.get(QStringLiteral("/stations/nearby?%1").arg(query.toString(QUrl::FullyEncoded)),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_statusLabel->hide();
                  qDeleteAll(m_cards);
                  m_cards.clear();
                  const QJsonArray stations = data.toArray();
                  for (const QJsonValue &value : stations) {
                      auto *card = new StationCard(Station::fromJson(value.toObject()), this);
                      connect(card, &StationCard::clicked, this, &StationListView::stationSelected);
                      connect(card, &StationCard::navigateRequested, this, &StationListView::navigateRequested);
                      m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
                      m_cards.append(card);
                  }
                  if (stations.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("附近没有可用充电站"));
                      m_statusLabel->show();
                      return;
                  }
                  applyFilter();
                  loadRecommendation();
              },
              [this](const ApiError &error) {
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}

void StationListView::applyFilter()
{
    const QString filter = m_searchEdit->text().trimmed();
    for (StationCard *card : m_cards) {
        card->setVisible(card->matches(filter));
    }
}

void StationListView::loadRecommendation()
{
    m_api.get(QStringLiteral("/forecasts?horizon=1h"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  const QJsonArray points = data.toObject().value(QLatin1String("points")).toArray();
                  const Station *best = nullptr;
                  int bestAvailable = -1;
                  double bestConfidence = 0;
                  for (const QJsonValue &value : points) {
                      const QJsonObject point = value.toObject();
                      const int stationId = point.value(QLatin1String("stationId")).toInt();
                      for (const StationCard *card : m_cards) {
                          const Station &station = card->station();
                          if (station.id != stationId) {
                              continue;
                          }
                          const int available = point.value(QLatin1String("availableChargerCount")).toInt();
                          const double confidence = point.value(QLatin1String("confidence")).toDouble();
                          if (available > bestAvailable
                              || (available == bestAvailable && confidence > bestConfidence)) {
                              best = &station;
                              bestAvailable = available;
                              bestConfidence = confidence;
                          }
                      }
                  }
                  if (best) {
                      m_recommendedStation = *best;
                      m_hasRecommendation = true;
                      m_bannerButton->setText(
                          QStringLiteral("🤖 AI 为您推荐：%1 · 预计 1 小时后空闲 %2 桩 · 置信度 %3%")
                              .arg(best->name)
                              .arg(bestAvailable)
                              .arg(qRound(bestConfidence * 100)));
                      m_bannerButton->setEnabled(true);
                      m_bannerButton->show();
                  }
              },
              [this](const ApiError &) {
                  const Station *best = nullptr;
                  double bestRatio = -1;
                  for (const StationCard *card : m_cards) {
                      const Station &station = card->station();
                      if (station.chargerCount == 0) {
                          continue;
                      }
                      const double ratio = double(station.availableChargerCount) / station.chargerCount;
                      if (ratio > bestRatio) {
                          best = &station;
                          bestRatio = ratio;
                      }
                  }
                  if (best && bestRatio > 0) {
                      m_recommendedStation = *best;
                      m_hasRecommendation = true;
                      m_bannerButton->setText(
                          QStringLiteral("🤖 智能推荐：%1 · 当前空闲率高，预计无需排队").arg(best->name));
                      m_bannerButton->setEnabled(true);
                      m_bannerButton->show();
                  }
              });
}
