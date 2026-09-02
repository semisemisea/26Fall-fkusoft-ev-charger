#include "ChargingTab.h"

#include "ChargingView.h"
#include "SettleView.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

ChargingTab::ChargingTab(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    buildPreparePage();

    m_chargingView = new ChargingView(m_api, this);
    m_settleView = new SettleView(m_session, m_api, this);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_preparePage);
    m_stack->addWidget(m_chargingView);
    m_stack->addWidget(m_settleView);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_chargingView, &ChargingView::orderStopped, this, [this](const Order &order) {
        showSettlement(order);
    });
    connect(m_settleView, &SettleView::settled, this, &ChargingTab::orderSettled);
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

    m_startButton = new QPushButton(QStringLiteral("启动\n充电"), page);
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

void ChargingTab::checkActiveOrder()
{
    m_api.get(QStringLiteral("/me/active-order"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  if (data.isNull()) {
                      showPrepare();
                      return;
                  }
                  const Order order = Order::fromJson(data.toObject());
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
    m_chargingView->open(order);
    m_stack->setCurrentWidget(m_chargingView);
}

void ChargingTab::showSettlement(const Order &order)
{
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
