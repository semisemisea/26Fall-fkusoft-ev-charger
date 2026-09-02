#include "StationDetailView.h"

#include "common/Format.h"
#include "models/Charger.h"

#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

StationDetailView::StationDetailView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_backButton = new QPushButton(QStringLiteral("← 返回"), this);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));

    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(QStringLiteral("color: #777;"));
    m_infoLabel->setWordWrap(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #d33;"));

    auto *chargersContainer = new QWidget(this);
    m_chargersLayout = new QVBoxLayout(chargersContainer);
    m_chargersLayout->setContentsMargins(0, 0, 0, 0);
    m_chargersLayout->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(chargersContainer);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(m_backButton);
    headerRow->addStretch();
    headerRow->addWidget(m_nameLabel);
    headerRow->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(headerRow);
    layout->addWidget(m_infoLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(scrollArea);

    connect(m_backButton, &QPushButton::clicked, this, &StationDetailView::backRequested);
}

void StationDetailView::open(const Station &station)
{
    m_station = station;
    m_nameLabel->setText(station.name);
    m_infoLabel->setText(QStringLiteral("%1\n%2 元/度 · 空闲 %3/%4")
                             .arg(station.address, fenToYuan(station.pricePerKwhFen))
                             .arg(station.availableChargerCount)
                             .arg(station.chargerCount));
    loadChargers();
}

void StationDetailView::loadChargers()
{
    m_statusLabel->setText(QStringLiteral("加载中..."));
    m_statusLabel->show();

    m_api.get(QStringLiteral("/stations/%1/chargers").arg(m_station.id),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_statusLabel->hide();
                  while (m_chargersLayout->count() > 1) {
                      QLayoutItem *item = m_chargersLayout->takeAt(0);
                      item->widget()->deleteLater();
                      delete item;
                  }
                  const QJsonArray chargers = data.toArray();
                  for (const QJsonValue &value : chargers) {
                      const Charger charger = Charger::fromJson(value.toObject());

                      auto *row = new QFrame(this);
                      row->setStyleSheet(
                          QStringLiteral("QFrame { background: white; border: 1px solid #ddd; border-radius: 8px; }"
                                         "QLabel { border: none; }"));
                      auto *codeLabel = new QLabel(charger.code, row);
                      codeLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));

                      const QString color = Charger::statusColor(charger.status);
                      auto *statusLabel = new QLabel(Charger::statusLabel(charger.status), row);
                      statusLabel->setStyleSheet(
                          QStringLiteral("color: white; background: %1; border-radius: 6px; padding: 2px 8px;").arg(color));

                      auto *rowLayout = new QHBoxLayout(row);
                      rowLayout->setContentsMargins(10, 6, 10, 6);
                      rowLayout->addWidget(codeLabel);
                      rowLayout->addWidget(new QLabel(Charger::typeLabel(charger), row));
                      rowLayout->addWidget(new QLabel(QStringLiteral("%1 kW").arg(charger.powerKw, 0, 'f', 1), row));
                      rowLayout->addStretch();
                      rowLayout->addWidget(statusLabel);
                      if (charger.status == QLatin1String("available")) {
                          auto *chargeButton = new QPushButton(QStringLiteral("充电"), row);
                          chargeButton->setStyleSheet(QStringLiteral("padding: 4px 14px;"));
                          connect(chargeButton, &QPushButton::clicked, this,
                                  [this, charger] { emit chargeRequested(charger); });
                          rowLayout->addWidget(chargeButton);
                      }

                      m_chargersLayout->insertWidget(m_chargersLayout->count() - 1, row);
                  }
                  if (chargers.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("站内暂无电桩"));
                      m_statusLabel->show();
                  }
              },
              [this](const ApiError &error) {
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}
