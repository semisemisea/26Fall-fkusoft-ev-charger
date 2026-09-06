#include "StationDetailView.h"

#include "common/Format.h"
#include "models/Charger.h"
#include "widgets/AppIcons.h"
#include "widgets/BackButton.h"
#include "widgets/ScaleButton.h"
#include "widgets/Spinner.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPainter>
#include <QVBoxLayout>

StationDetailView::StationDetailView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_bgPixmap.load(QStringLiteral(":/backgrounds/StationDetailView.png"));
    m_backButton = new BackButton(this);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName(QStringLiteral("heroTitle"));

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("muted"));
    m_infoLabel->setStyleSheet(QStringLiteral("font-size: 15px;"));
    m_infoLabel->setWordWrap(true);

    const QString tagStyle = QStringLiteral(
        "background:#e8eae7;border-radius:8px;color:#000000;"
        "padding:5px 9px;");
    m_distanceLabel = new QLabel(this);
    m_distanceLabel->setStyleSheet(tagStyle);
    m_distanceLabel->setTextFormat(Qt::RichText);
    m_distanceLabel->setAlignment(Qt::AlignCenter);
    m_distanceLabel->setFixedHeight(32);
    m_distanceLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_priceLabel = new QLabel(this);
    m_priceLabel->setStyleSheet(tagStyle);
    m_priceLabel->setTextFormat(Qt::RichText);
    m_priceLabel->setAlignment(Qt::AlignCenter);
    m_priceLabel->setFixedHeight(32);
    m_priceLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_availabilityLabel = new QLabel(this);
    m_availabilityLabel->setStyleSheet(tagStyle);
    m_availabilityLabel->setTextFormat(Qt::RichText);
    m_availabilityLabel->setAlignment(Qt::AlignCenter);
    m_availabilityLabel->setFixedHeight(32);
    m_availabilityLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_navButton = new QPushButton(this);
    m_navButton->setFixedSize(64, 48);
    m_navButton->setIcon(AppIcons::turnRight(Qt::white, 24));
    m_navButton->setIconSize(QSize(24, 24));
    m_navButton->setCursor(Qt::PointingHandCursor);
    m_navButton->setStyleSheet(QStringLiteral(
        "background:#000000;border:none;border-radius:24px;"));

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("error"));

    m_spinner = new Spinner(this);
    m_spinner->hide();

    auto *statusRow = new QWidget(this);
    auto *statusRowLayout = new QHBoxLayout(statusRow);
    statusRowLayout->setContentsMargins(0, 0, 0, 0);
    statusRowLayout->setSpacing(8);
    statusRowLayout->addStretch();
    statusRowLayout->addWidget(m_spinner);
    statusRowLayout->addWidget(m_statusLabel);
    statusRowLayout->addStretch();

    auto *chargersContainer = new QWidget(this);
    m_chargersLayout = new QVBoxLayout(chargersContainer);
    m_chargersLayout->setContentsMargins(0, 0, 0, 0);
    m_chargersLayout->setSpacing(10);
    m_chargersLayout->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(chargersContainer);

    m_backButton->move(12, 12);
    m_backButton->raise();

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(m_nameLabel);
    headerRow->addStretch();

    m_bgSpacer = new QWidget(this);
    m_bgSpacer->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_reserveButton = new QPushButton(QStringLiteral("预约"), this);
    m_reserveButton->setCursor(Qt::PointingHandCursor);

    m_chargeButton = new QPushButton(QStringLiteral("充电"), this);
    m_chargeButton->setCursor(Qt::PointingHandCursor);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(12);
    bottomRow->addWidget(m_reserveButton);
    bottomRow->addWidget(m_chargeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 0, 20, 12);
    layout->addWidget(m_bgSpacer);
    layout->addLayout(headerRow);
    layout->addWidget(m_infoLabel);

    auto *tagRow = new QHBoxLayout;
    tagRow->setSpacing(8);
    tagRow->addWidget(m_distanceLabel, 0, Qt::AlignBottom);
    tagRow->addWidget(m_priceLabel, 0, Qt::AlignBottom);
    tagRow->addWidget(m_availabilityLabel, 0, Qt::AlignBottom);
    tagRow->addStretch();
    tagRow->addWidget(m_navButton, 0, Qt::AlignBottom);
    layout->addLayout(tagRow);

    layout->addWidget(statusRow);
    layout->addWidget(scrollArea, 1);
    layout->addLayout(bottomRow);

    connect(m_backButton, &QPushButton::clicked, this, &StationDetailView::backRequested);
    connect(m_navButton, &QPushButton::clicked, this,
            [this] { emit navigateRequested(m_station); });
    connect(m_reserveButton, &QPushButton::clicked, this, [this] {
        if (m_hasSelection && m_selectedCharger.status == QLatin1String("available"))
            emit reservationRequested(m_selectedCharger);
    });
    connect(m_chargeButton, &QPushButton::clicked, this, [this] {
        if (m_hasSelection && m_selectedCharger.status == QLatin1String("available"))
            emit chargeRequested(m_selectedCharger);
    });

    updateBottomButtons();

    if (!m_bgPixmap.isNull() && m_bgPixmap.width() > 0)
        m_bgSpacer->setFixedHeight(m_bgPixmap.height() * width() / m_bgPixmap.width());
}

void StationDetailView::open(const Station &station)
{
    m_station = station;
    m_nameLabel->setText(station.name);
    m_infoLabel->setText(station.address);
    m_distanceLabel->setText(QStringLiteral(
        "<span style='font-size:16px;font-weight:600;'>%1</span>"
        "<span style='font-size:12px;'> km</span>"
    ).arg(QString::number(station.distanceKm, 'f', 1)));

    m_priceLabel->setText(QStringLiteral(
        "<span style='font-size:12px;'>￥ </span>"
        "<span style='font-size:16px;font-weight:600;'>%1</span>"
        "<span style='font-size:12px;'> /度</span>"
    ).arg(fenToYuan(station.pricePerKwhFen)));
    m_availabilityLabel->setText(QStringLiteral(
        "<span style='font-size:12px;'>空闲 </span>"
        "<span style='font-size:16px;font-weight:600;'>%1/%2</span>"
    ).arg(station.availableChargerCount).arg(station.chargerCount));
    loadChargers();
}

void StationDetailView::loadChargers()
{
    m_spinner->show();
    m_statusLabel->hide();

    m_hasSelection = false;
    m_selectedRow = nullptr;
    m_chargers.clear();
    updateBottomButtons();

    m_api.get(QStringLiteral("/stations/%1/chargers").arg(m_station.id),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  while (m_chargersLayout->count() > 1) {
                      QLayoutItem *item = m_chargersLayout->takeAt(0);
                      item->widget()->deleteLater();
                      delete item;
                  }
                  const QJsonArray chargers = data.toArray();
                  for (const QJsonValue &value : chargers) {
                      const Charger charger = Charger::fromJson(value.toObject());
                      m_chargers.append(charger);

                      auto *row = new QFrame(this);
                      row->setObjectName(QStringLiteral("chargerRow"));
                      row->setProperty("chargerId", charger.id);
                      row->installEventFilter(this);
                      row->setCursor(Qt::PointingHandCursor);

                      auto *rowLayout = new QVBoxLayout(row);
                      rowLayout->setContentsMargins(12, 10, 12, 10);
                      rowLayout->setSpacing(6);

                      auto *topRow = new QHBoxLayout;
                      topRow->setSpacing(8);

                      auto *codeLabel = new QLabel(charger.code, row);
                      codeLabel->setStyleSheet(QStringLiteral(
                          "color:#000000;font-size:17px;font-weight:bold;"));
                      topRow->addWidget(codeLabel);

                      const QString color = Charger::statusColor(charger.status);
                      const QString bgColor = Charger::statusBgColor(charger.status);
                      auto *statusLabel = new QLabel(Charger::statusLabel(charger.status), row);
                      if (charger.status == QLatin1String("available")) {
                          statusLabel->setStyleSheet(QStringLiteral(
                              "background:#ffffff;color:#16A34A;border:1px solid #22C55E;"
                              "font-size:12px;font-weight:bold;padding:4px 10px;border-radius:8px;"));
                      } else if (charger.status == QLatin1String("charging")) {
                          statusLabel->setStyleSheet(QStringLiteral(
                              "background:#ffffff;color:#C2B700;border:1px solid #E3D700;"
                              "font-size:12px;font-weight:bold;padding:4px 10px;border-radius:8px;"));
                      } else if (charger.status == QLatin1String("reserved")) {
                          statusLabel->setStyleSheet(QStringLiteral(
                              "background:#ffffff;color:#C2B700;border:1px solid #E3D700;"
                              "font-size:12px;font-weight:bold;padding:4px 10px;border-radius:8px;"));
                      } else {
                          statusLabel->setStyleSheet(QStringLiteral(
                              "background:#ffffff;color:%1;border:1px solid %1;font-size:12px;"
                              "padding:4px 10px;border-radius:8px;").arg(color));
                      }
                      topRow->addWidget(statusLabel);
                      topRow->addStretch();

                      rowLayout->addLayout(topRow);

                      auto *detailRow = new QHBoxLayout;
                      detailRow->setSpacing(12);

                      auto *typeLabel = new QLabel(Charger::typeLabel(charger), row);
                      typeLabel->setStyleSheet(QStringLiteral("color:#6b7280;font-size:14px;"));
                      detailRow->addWidget(typeLabel);

                      auto *powerLabel = new QLabel(
                          QStringLiteral("%1 kW").arg(charger.powerKw, 0, 'f', 1), row);
                      powerLabel->setStyleSheet(QStringLiteral("color:#6b7280;font-size:14px;"));
                      detailRow->addWidget(powerLabel);
                      detailRow->addStretch();

                      rowLayout->addLayout(detailRow);

                      m_chargersLayout->insertWidget(m_chargersLayout->count() - 1, row);
                  }
                  if (chargers.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("站内暂无电桩"));
                      m_statusLabel->show();
                  }
              },
              [this](const ApiError &error) {
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}

bool StationDetailView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QVariant prop = obj->property("chargerId");
        if (prop.isValid()) {
            int chargerId = prop.toInt();
            for (const Charger &c : m_chargers) {
                if (c.id == chargerId) {
                    if (m_selectedRow)
                        m_selectedRow->setStyleSheet(QString());
                    m_selectedCharger = c;
                    m_hasSelection = true;
                    m_selectedRow = qobject_cast<QFrame*>(obj);
                    if (m_selectedRow) {
                        m_selectedRow->setStyleSheet(QStringLiteral(
                            "QFrame#chargerRow { background:#ffffff; border:2px solid #b0b4bc; border-radius:12px; }"));
                    }
                    updateBottomButtons();
                    break;
                }
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void StationDetailView::updateBottomButtons()
{
    bool available = m_hasSelection &&
                     m_selectedCharger.status == QLatin1String("available");

    m_reserveButton->setEnabled(available);
    m_chargeButton->setEnabled(available);

    if (available) {
        m_reserveButton->setStyleSheet(QStringLiteral(
            "background:#ffffff;border:1px solid #d1d5db;border-radius:22px;"
            "color:#1e293b;font-size:15px;padding:0 16px;min-height:44px;"));
        m_chargeButton->setStyleSheet(QStringLiteral(
            "background:#2BFF7D;"
            "border:1px solid #22C55E;border-radius:22px;color:#000000;font-size:15px;padding:0 16px;min-height:44px;"));
    } else {
        m_reserveButton->setStyleSheet(QStringLiteral(
            "background:#f3f4f6;border:1px solid #9ca3af;border-radius:22px;"
            "color:#9ca3af;font-size:15px;padding:0 16px;min-height:44px;"));
        m_chargeButton->setStyleSheet(QStringLiteral(
            "background:#e5e7eb;border:1px solid #9ca3af;border-radius:22px;"
            "color:#9ca3af;font-size:15px;padding:0 16px;min-height:44px;"));
    }
}

void StationDetailView::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_bgPixmap.isNull())
        return;
    QPainter painter(this);
    const int targetWidth = width();
    const int targetHeight = m_bgPixmap.height() * targetWidth / m_bgPixmap.width();
    painter.drawPixmap(QRect(0, 0, targetWidth, targetHeight), m_bgPixmap);

    if (targetHeight < height()) {
        QLinearGradient gradient(0, targetHeight, 0, height());
        gradient.setColorAt(0, QColor(QStringLiteral("#f6f7f8")));
        gradient.setColorAt(1, QColor(QStringLiteral("#e9ebed")));
        painter.fillRect(0, targetHeight, width(), height() - targetHeight, gradient);
    }
}

void StationDetailView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_bgSpacer && !m_bgPixmap.isNull() && m_bgPixmap.width() > 0) {
        const int h = m_bgPixmap.height() * width() / m_bgPixmap.width();
        m_bgSpacer->setFixedHeight(h);
    }
}