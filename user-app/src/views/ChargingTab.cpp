#include "ChargingTab.h"

#include "ChargingView.h"
#include "SettleView.h"
#include "widgets/ScaleButton.h"

#include <QDateTime>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

ChargingTab::ChargingTab(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    buildPreparePage();
    buildReservationPage();

    m_chargingView = new ChargingView(m_api, this);
    m_settleView = new SettleView(m_session, m_api, this);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_preparePage);
    m_stack->addWidget(m_chargingView);
    m_stack->addWidget(m_settleView);
    m_stack->addWidget(m_reservationPage);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_chargingView, &ChargingView::orderStopped, this, [this](const Order &order) {
        showSettlement(order);
    });
    connect(m_settleView, &SettleView::settled, this, [this] {
        emit activeOrderChanged(false);
        emit orderSettled();
    });
    connect(m_settleView, &SettleView::dismissed, this, &ChargingTab::returnHomeRequested);
    connect(m_settleView, &SettleView::returnHomeRequested, this, &ChargingTab::returnHomeRequested);
}

void ChargingTab::buildPreparePage()
{
    auto *page = new QWidget(this);

    auto *iconLabel = new QLabel(QStringLiteral("⚡"), page);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral("font-size: 52px;"));

    auto *titleLabel = new QLabel(QStringLiteral("准备充电"), page);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: bold;"));

    auto *tipLabel = new QLabel(QStringLiteral("输入电桩编号，如 S01-001"), page);
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLabel->setStyleSheet(QStringLiteral("color: #888;"));

    m_codeEdit = new QLineEdit(page);
    m_codeEdit->setPlaceholderText(QStringLiteral("请输入电桩编号"));
    m_codeEdit->setAlignment(Qt::AlignCenter);
    m_codeEdit->setClearButtonEnabled(true);

    m_hintLabel = new QLabel(page);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet(QStringLiteral("color: #e74c3c;"));
    m_hintLabel->hide();

    m_startButton = new ScaleButton(QStringLiteral("启动\n充电"), page);
    m_startButton->setObjectName(QStringLiteral("roundStartButton"));
    m_startButton->setFixedSize(150, 150);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 12, 32, 24);
    layout->addStretch(2);
    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addSpacing(4);
    layout->addWidget(tipLabel);
    layout->addSpacing(16);
    layout->addWidget(m_codeEdit);
    layout->addWidget(m_hintLabel);
    layout->addSpacing(20);
    layout->addWidget(m_startButton, 0, Qt::AlignCenter);
    layout->addStretch(3);

    connect(m_codeEdit, &QLineEdit::returnPressed, this, &ChargingTab::startWithCode);
    connect(m_startButton, &QPushButton::clicked, this, &ChargingTab::startWithCode);

    m_preparePage = page;
}

void ChargingTab::buildReservationPage()
{
    auto *page = new QWidget(this);

    auto *titleLabel = new QLabel(QStringLiteral("我的预约"), page);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: bold;"));

    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("reservationCard"));
    card->setStyleSheet(QStringLiteral(
        "QFrame#reservationCard { background: white; border: 1px solid #e8eaee; border-radius: 14px; }"
        "QLabel { border: none; }"));

    m_reservationStationLabel = new QLabel(card);
    m_reservationStationLabel->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: bold;"));
    m_reservationStationLabel->setWordWrap(true);

    m_reservationChargerLabel = new QLabel(card);
    m_reservationChargerLabel->setStyleSheet(QStringLiteral("color: #777;"));

    m_countdownLabel = new QLabel(card);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: bold; color: #1d5cff;"));

    auto *countdownTipLabel = new QLabel(QStringLiteral("保留时长倒计时"), card);
    countdownTipLabel->setAlignment(Qt::AlignCenter);
    countdownTipLabel->setStyleSheet(QStringLiteral("color: #999;"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(6);
    cardLayout->addWidget(m_reservationStationLabel);
    cardLayout->addWidget(m_reservationChargerLabel);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(m_countdownLabel);
    cardLayout->addWidget(countdownTipLabel);

    m_reservationStartButton = new ScaleButton(QStringLiteral("启动充电"), page);
    m_reservationStartButton->setObjectName(QStringLiteral("primaryButton"));
    m_reservationCancelButton = new ScaleButton(QStringLiteral("取消预约"), page);
    m_reservationCancelButton->setObjectName(QStringLiteral("outlineDangerButton"));

    m_reservationHintLabel = new QLabel(page);
    m_reservationHintLabel->setAlignment(Qt::AlignCenter);
    m_reservationHintLabel->setWordWrap(true);
    m_reservationHintLabel->setStyleSheet(QStringLiteral("color: #e74c3c;"));
    m_reservationHintLabel->hide();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 12, 24, 24);
    layout->addStretch(2);
    layout->addWidget(titleLabel);
    layout->addSpacing(16);
    layout->addWidget(card);
    layout->addSpacing(8);
    layout->addWidget(m_reservationHintLabel);
    layout->addSpacing(12);
    layout->addWidget(m_reservationStartButton);
    layout->addWidget(m_reservationCancelButton);
    layout->addStretch(3);

    connect(m_reservationStartButton, &QPushButton::clicked, this, &ChargingTab::startFromReservation);
    connect(m_reservationCancelButton, &QPushButton::clicked, this, &ChargingTab::cancelReservation);

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &ChargingTab::updateCountdown);

    m_reservationPage = page;
}

void ChargingTab::checkActiveOrder()
{
    m_api.get(QStringLiteral("/me/active-order"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  if (data.isNull()) {
                      checkActiveReservation();
                      return;
                  }
                  const Order order = Order::fromJson(data.toObject());
                  emit activeOrderChanged(true);
                  if (order.status == QLatin1String("charging")) {
                      showCharging(order);
                  } else {
                      showSettlement(order);
                  }
              },
              [this](const ApiError &error) {
                  fail(error.message.isEmpty() ? error.code : error.message);
              });
}

void ChargingTab::showCharging(const Order &order)
{
    emit activeOrderChanged(true);
    m_chargingView->open(order);
    m_stack->setCurrentWidget(m_chargingView);
}

void ChargingTab::showSettlement(const Order &order)
{
    emit activeOrderChanged(true);
    m_settleView->open(order);
    m_stack->setCurrentWidget(m_settleView);
}

void ChargingTab::showPrepare()
{
    m_stack->setCurrentWidget(m_preparePage);
}

void ChargingTab::startWithCode()
{
    const QString code = m_codeEdit->text().trimmed().toUpper();
    if (code.isEmpty()) {
        m_hintLabel->setText(QStringLiteral("请输入电桩编号"));
        m_hintLabel->show();
        return;
    }

    m_startButton->setEnabled(false);
    m_hintLabel->setText(QStringLiteral("正在查找电桩..."));
    m_hintLabel->show();
    m_pendingCode = code;
    fetchNearbyStations();
}

void ChargingTab::fetchNearbyStations()
{
    QUrlQuery query;
    query.addQueryItem(QLatin1String("latitude"), QString::number(m_session.latitude()));
    query.addQueryItem(QLatin1String("longitude"), QString::number(m_session.longitude()));

    m_api.get(QStringLiteral("/stations/nearby?%1").arg(query.toString(QUrl::FullyEncoded)),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_candidateStationIds.clear();
                  const QJsonArray stations = data.toArray();
                  for (const QJsonValue &value : stations) {
                      m_candidateStationIds.append(value.toObject().value(QLatin1String("id")).toInt());
                  }
                  m_candidateIndex = 0;
                  tryNextCandidate();
              },
              [this](const ApiError &error) {
                  fail(error.message.isEmpty() ? error.code : error.message);
              });
}

void ChargingTab::tryNextCandidate()
{
    if (m_candidateIndex >= m_candidateStationIds.size()) {
        fail(QStringLiteral("未找到编号为 %1 的电桩").arg(m_pendingCode));
        return;
    }
    const int stationId = m_candidateStationIds.at(m_candidateIndex++);
    m_api.get(QStringLiteral("/stations/%1/chargers").arg(stationId),
              [this](const QJsonValue &data, const QJsonObject &) {
                  for (const QJsonValue &value : data.toArray()) {
                      const QJsonObject charger = value.toObject();
                      if (charger.value(QLatin1String("code")).toString().toUpper() == m_pendingCode) {
                          createOrder(charger.value(QLatin1String("id")).toInt());
                          return;
                      }
                  }
                  tryNextCandidate();
              },
              [this](const ApiError &) { tryNextCandidate(); });
}

void ChargingTab::createOrder(int chargerId)
{
    QJsonObject body;
    body.insert(QLatin1String("chargerId"), chargerId);
    m_api.post(QStringLiteral("/orders"), body,
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_startButton->setEnabled(true);
                   m_hintLabel->hide();
                   showCharging(Order::fromJson(data.toObject()));
               },
               [this](const ApiError &error) {
                   m_startButton->setEnabled(true);
                   if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                       checkActiveOrder();
                       return;
                   }
                   fail(error.message.isEmpty() ? error.code : error.message);
               });
}

void ChargingTab::fail(const QString &message)
{
    m_startButton->setEnabled(true);
    m_hintLabel->setText(message);
    m_hintLabel->show();
    showPrepare();
}

void ChargingTab::showReservation(const Reservation &reservation)
{
    m_reservation = reservation;
    m_reservationStationLabel->setText(reservation.stationName);
    m_reservationChargerLabel->setText(QStringLiteral("电桩 %1 · 已为您保留").arg(reservation.chargerCode));
    m_reservationHintLabel->hide();
    updateCountdown();
    m_countdownTimer->start();
    emit activeOrderChanged(true);
    m_stack->setCurrentWidget(m_reservationPage);
}

void ChargingTab::checkActiveReservation()
{
    m_api.get(QStringLiteral("/reservations?status=active"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  const QJsonArray items = data.toArray();
                  if (items.isEmpty()) {
                      emit activeOrderChanged(false);
                      showPrepare();
                      return;
                  }
                  showReservation(Reservation::fromJson(items.first().toObject()));
              },
              [this](const ApiError &) {
                  emit activeOrderChanged(false);
                  showPrepare();
              });
}

void ChargingTab::startFromReservation()
{
    setReservationBusy(true);
    const int reservationId = m_reservation.id;
    const int chargerId = m_reservation.chargerId;
    QJsonObject body;
    body.insert(QLatin1String("chargerId"), chargerId);
    body.insert(QLatin1String("reservationId"), reservationId);
    m_api.post(QStringLiteral("/orders"), body,
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_countdownTimer->stop();
                   setReservationBusy(false);
                   showCharging(Order::fromJson(data.toObject()));
               },
               [this](const ApiError &error) {
                   setReservationBusy(false);
                   if (error.code == QLatin1String("ACTIVE_ORDER_EXISTS")) {
                       checkActiveOrder();
                       return;
                   }
                   m_reservationHintLabel->setText(error.message.isEmpty() ? error.code : error.message);
                   m_reservationHintLabel->show();
                   if (error.code != QLatin1String("CHARGER_UNAVAILABLE")
                       && error.code != QLatin1String("INVALID_STATE_TRANSITION")) {
                       return;
                   }
                   QTimer::singleShot(1200, this, &ChargingTab::checkActiveOrder);
               });
}

void ChargingTab::cancelReservation()
{
    setReservationBusy(true);
    const int reservationId = m_reservation.id;
    m_api.post(QStringLiteral("/reservations/%1/cancel").arg(reservationId), {},
               [this](const QJsonValue &, const QJsonObject &) {
                   m_countdownTimer->stop();
                   setReservationBusy(false);
                   m_hintLabel->setText(QStringLiteral("预约已取消"));
                   m_hintLabel->show();
                   emit activeOrderChanged(false);
                   showPrepare();
               },
               [this](const ApiError &error) {
                   setReservationBusy(false);
                   m_reservationHintLabel->setText(error.message.isEmpty() ? error.code : error.message);
                   m_reservationHintLabel->show();
                   QTimer::singleShot(1200, this, &ChargingTab::checkActiveOrder);
               });
}

void ChargingTab::updateCountdown()
{
    const qint64 remaining = QDateTime::currentDateTimeUtc().secsTo(m_reservation.expiresAt);
    if (remaining <= 0) {
        m_countdownTimer->stop();
        m_countdownLabel->setText(QStringLiteral("00:00"));
        m_reservationHintLabel->setText(QStringLiteral("预约已过期，电桩已释放"));
        m_reservationHintLabel->show();
        QTimer::singleShot(1200, this, &ChargingTab::checkActiveOrder);
        return;
    }
    m_countdownLabel->setText(QStringLiteral("%1:%2")
                                  .arg(remaining / 60, 2, 10, QLatin1Char('0'))
                                  .arg(remaining % 60, 2, 10, QLatin1Char('0')));
}

void ChargingTab::setReservationBusy(bool busy)
{
    m_reservationStartButton->setEnabled(!busy);
    m_reservationCancelButton->setEnabled(!busy);
}
