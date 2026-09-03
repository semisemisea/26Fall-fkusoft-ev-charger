#include "ChargingView.h"

#include "app/ChargePollThread.h"
#include "common/Format.h"
#include "widgets/ChargeRingWidget.h"

#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

ChargingView::ChargingView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_headerLabel = new QLabel(this);
    m_headerLabel->setAlignment(Qt::AlignCenter);
    m_headerLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));

    m_ringWidget = new ChargeRingWidget(this);

    m_energyLabel = new QLabel(this);
    m_energyLabel->setAlignment(Qt::AlignCenter);
    m_energyLabel->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: bold;"));

    m_durationLabel = new QLabel(this);
    m_durationLabel->setAlignment(Qt::AlignCenter);
    m_durationLabel->setStyleSheet(QStringLiteral("color: #555; font-size: 15px;"));

    m_amountLabel = new QLabel(this);
    m_amountLabel->setAlignment(Qt::AlignCenter);
    m_amountLabel->setStyleSheet(QStringLiteral("color: #e74c3c; font-size: 16px; font-weight: bold;"));

    m_orderLabel = new QLabel(this);
    m_orderLabel->setAlignment(Qt::AlignCenter);
    m_orderLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));

    m_stopButton = new QPushButton(QStringLiteral("结束充电"), this);
    m_stopButton->setObjectName(QStringLiteral("dangerButton"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 24, 16, 16);
    layout->addWidget(m_headerLabel);
    layout->addStretch(2);
    layout->addWidget(m_ringWidget, 0, Qt::AlignCenter);
    layout->addSpacing(12);
    layout->addWidget(m_energyLabel);
    layout->addWidget(m_durationLabel);
    layout->addSpacing(6);
    layout->addWidget(m_amountLabel);
    layout->addWidget(m_orderLabel);
    layout->addStretch(2);
    layout->addWidget(m_stopButton);

    m_pollThread = new ChargePollThread(this);
    connect(m_pollThread, &ChargePollThread::meterUpdated, this, [this](const QJsonObject &orderObject) {
        m_order = Order::fromJson(orderObject);
        updateDisplay(m_order);
    });
    connect(m_stopButton, &QPushButton::clicked, this, &ChargingView::stopCharging);
}

void ChargingView::open(const Order &order)
{
    m_order = order;
    updateDisplay(order);
    m_pollThread->configure(order.id, m_api.baseUrl(), m_api.accessToken());
    m_pollThread->start();
}

void ChargingView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pollThread->requestStop();
}

void ChargingView::updateDisplay(const Order &order)
{
    m_headerLabel->setText(QStringLiteral("⚡ 充电中 · %1 · 电桩 %2")
                               .arg(order.stationName.isEmpty() ? QStringLiteral("充电站") : order.stationName,
                                    order.chargerCode.isEmpty() ? QStringLiteral("-") : order.chargerCode));
    m_energyLabel->setText(QStringLiteral("%1 度").arg(order.energyKwh, 0, 'f', 1));
    m_durationLabel->setText(QStringLiteral("已充时长 %1 · 单价 ￥%2/度")
                                 .arg(formatDuration(order.durationMinutes),
                                      fenToYuan(order.unitPriceFenPerKwh)));
    m_amountLabel->setText(QStringLiteral("预估费用 ￥%1").arg(fenToYuan(order.amountFen)));
    m_orderLabel->setText(QStringLiteral("订单号 %1").arg(order.orderNo));
}

void ChargingView::stopCharging()
{
    const auto choice = QMessageBox::question(this, QStringLiteral("停止充电"),
                                              QStringLiteral("确定停止充电并生成账单吗？"));
    if (choice != QMessageBox::Yes) {
        return;
    }

    m_pollThread->requestStop();
    m_stopButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/stop").arg(m_order.id), {},
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_stopButton->setEnabled(true);
                   emit orderStopped(Order::fromJson(data.toObject()));
               },
               [this](const ApiError &error) {
                   m_stopButton->setEnabled(true);
                   m_pollThread->configure(m_order.id, m_api.baseUrl(), m_api.accessToken());
                   m_pollThread->start();
                   QMessageBox::warning(this, QStringLiteral("停止失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}

QString ChargingView::formatDuration(int minutes)
{
    const QTime duration = QTime(0, 0).addSecs(minutes * 60);
    return duration.toString(QStringLiteral("HH:mm:ss"));
}
