#include "NavigationView.h"

#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace {
struct ModeOption
{
    const char *label;
    const char *value;
};

const ModeOption kModeOptions[] = {
    {"驾车", "driving"},
    {"步行", "walking"},
};
}

NavigationView::NavigationView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    auto *backButton = new QPushButton(QStringLiteral("←"), this);
    backButton->setFixedWidth(44);
    auto *titleLabel = new QLabel(this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
    titleLabel->setObjectName(QStringLiteral("stationTitle"));

    m_modeCombo = new QComboBox(this);
    for (const ModeOption &option : kModeOptions) {
        m_modeCombo->addItem(QString::fromUtf8(option.label), QLatin1String(option.value));
    }

    m_navigateButton = new QPushButton(QStringLiteral("开始导航"), this);
    m_navigateButton->setStyleSheet(QStringLiteral("padding: 8px 16px;"));

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet(QStringLiteral("color: #2a6fdb;"));
    m_summaryLabel->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #d33;"));
    m_statusLabel->hide();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 12, 12, 12);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(backButton);
    headerRow->addStretch();
    headerRow->addWidget(titleLabel);
    headerRow->addStretch();
    m_layout->addLayout(headerRow);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("出行方式："), this));
    modeRow->addWidget(m_modeCombo);
    modeRow->addWidget(m_navigateButton);
    modeRow->addStretch();
    m_layout->addLayout(modeRow);
    m_layout->addWidget(m_summaryLabel);
    m_layout->addWidget(m_statusLabel);

    connect(backButton, &QPushButton::clicked, this, &NavigationView::backRequested);
    connect(m_navigateButton, &QPushButton::clicked, this, &NavigationView::requestRoute);
    connect(m_modeCombo, &QComboBox::activated, this, [this] {
        if (m_webView) {
            requestRoute();
        }
    });
}

void NavigationView::open(const Station &station)
{
    m_station = station;
    findChild<QLabel *>(QStringLiteral("stationTitle"))->setText(station.name);
    m_summaryLabel->hide();
    m_statusLabel->hide();
    requestRoute();
}

void NavigationView::requestRoute()
{
    m_navigateButton->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在规划路线..."));
    m_statusLabel->show();

    QUrlQuery query;
    query.addQueryItem(QLatin1String("fromLatitude"), QString::number(m_session.latitude()));
    query.addQueryItem(QLatin1String("fromLongitude"), QString::number(m_session.longitude()));
    query.addQueryItem(QLatin1String("toLatitude"), QString::number(m_station.latitude));
    query.addQueryItem(QLatin1String("toLongitude"), QString::number(m_station.longitude));
    query.addQueryItem(QLatin1String("mode"), m_modeCombo->currentData().toString());
    query.addQueryItem(QLatin1String("fromName"), QStringLiteral("我的位置"));
    query.addQueryItem(QLatin1String("toName"), m_station.name);

    m_api.get(QStringLiteral("/locations/routes?%1").arg(query.toString(QUrl::FullyEncoded)),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_navigateButton->setEnabled(true);
                  const QJsonObject object = data.toObject();
                  const double distanceKm = object.value(QLatin1String("distanceM")).toInt() / 1000.0;
                  const int minutes = qRound(object.value(QLatin1String("durationSec")).toInt() / 60.0);
                  m_summaryLabel->setText(QStringLiteral("全程约 %1 公里 · 约 %2 分钟（%3）")
                                              .arg(distanceKm, 0, 'f', 1)
                                              .arg(minutes)
                                              .arg(m_modeCombo->currentText()));
                  m_summaryLabel->show();
                  m_statusLabel->hide();

                  const QUrl mapUrl(object.value(QLatin1String("mapUrl")).toString());
                  if (!mapUrl.isValid()) {
                      return;
                  }
                  if (!m_webView) {
                      m_webView = new QWebEngineView(this);
                      m_webView->page()->profile()->setHttpUserAgent(QStringLiteral(
                          "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"));
                      m_layout->addWidget(m_webView, 1);
                  }
                  m_webView->load(mapUrl);
              },
              [this](const ApiError &error) {
                  m_navigateButton->setEnabled(true);
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}
