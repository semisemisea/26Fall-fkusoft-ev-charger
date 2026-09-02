#include "ChargingView.h"

#include "common/Format.h"

#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kPollIntervalMs = 5000;
}

ChargingView::ChargingView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold; color: #2e9e5b;"));

    m_energyLabel = new QLabel(this);
    m_energyLabel->setAlignment(Qt::AlignCenter);
    m_energyLabel->setStyleSheet(QStringLiteral("font-size: 36px; font-weight: bold;"));

    m_durationLabel = new QLabel(this);
    m_durationLabel->setAlignment(Qt::AlignCenter);

    m_amountLabel = new QLabel(this);
    m_amountLabel->setAlignment(Qt::AlignCenter);
    m_amountLabel->setStyleSheet(QStringLiteral("color: #2a6fdb;"));

    m_orderLabel = new QLabel(this);
    m_orderLabel->setAlignment(Qt::AlignCenter);
    m_orderLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));

    m_stopButton = new QPushButton(QStringLiteral("停止充电"), this);
    m_stopButton->setStyleSheet(QStringLiteral("padding: 10px; font-size: 16px;"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addStretch(2);
    layout->addWidget(m_statusLabel);
    layout->addSpacing(16);
    layout->addWidget(m_energyLabel);
    layout->addWidget(m_durationLabel);
    layout->addSpacing(8);
    layout->addWidget(m_amountLabel);
    layout->addWidget(m_orderLabel);
    layout->addStretch(2);
    layout->addWidget(m_stopButton);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &ChargingView::poll);
    connect(m_stopButton, &QPushButton::clicked, this, &ChargingView::stopCharging);
}

void ChargingView::open(const Order &order)
{
    m_order = order;
    updateDisplay(order);
    m_pollTimer->start();
}

void ChargingView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pollTimer->stop();
}

void ChargingView::poll()
{
    m_api.get(QStringLiteral("/orders/%1").arg(m_order.id),
              [this](const QJsonValue &data, const QJsonObject &) {
                  const Order order = Order::fromJson(data.toObject());
                  if (order.status == QLatin1String("charging")) {
                      m_order = order;
                      updateDisplay(order);
                  }
              },
              [this](const ApiError &) { m_pollTimer->stop(); });
}

void ChargingView::updateDisplay(const Order &order)
{
    m_statusLabel->setText(QStringLiteral("⚡ 充电中"));
    m_energyLabel->setText(QStringLiteral("%1 度").arg(order.energyKwh, 0, 'f', 1));
    m_durationLabel->setText(QStringLiteral("已充 %1 分钟 · 单价 %2 元/度").arg(order.durationMinutes).arg(fenToYuan(order.unitPriceFenPerKwh)));
    m_amountLabel->setText(QStringLiteral("预计金额 %1 元").arg(fenToYuan(order.amountFen)));
    m_orderLabel->setText(QStringLiteral("订单号 %1").arg(order.orderNo));
}

void ChargingView::stopCharging()
{
    const auto choice = QMessageBox::question(this, QStringLiteral("停止充电"),
                                              QStringLiteral("确定停止充电并生成账单吗？"));
    if (choice != QMessageBox::Yes) {
        return;
    }

    m_pollTimer->stop();
    m_stopButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/stop").arg(m_order.id), {},
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_stopButton->setEnabled(true);
                   emit orderStopped(Order::fromJson(data.toObject()));
               },
               [this](const ApiError &error) {
                   m_stopButton->setEnabled(true);
                   m_pollTimer->start();
                   QMessageBox::warning(this, QStringLiteral("停止失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}
