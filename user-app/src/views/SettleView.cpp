#include "SettleView.h"

#include "common/Format.h"
#include "models/Order.h"

#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr qlonglong kQuickTopUpFen = 5000;
}

SettleView::SettleView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    auto *title = new QLabel(QStringLiteral("订单结算"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));

    m_energyLabel = new QLabel(this);
    m_durationLabel = new QLabel(this);
    m_priceLabel = new QLabel(this);

    m_totalLabel = new QLabel(this);
    m_totalLabel->setAlignment(Qt::AlignCenter);
    m_totalLabel->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: bold; color: #2a6fdb;"));

    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setStyleSheet(QStringLiteral("color: #d33;"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->hide();

    m_payButton = new QPushButton(QStringLiteral("确认支付（钱包）"), this);
    m_payButton->setStyleSheet(QStringLiteral("padding: 10px; font-size: 16px;"));

    m_topUpButton = new QPushButton(QStringLiteral("快捷充值 50 元"), this);
    m_topUpButton->hide();

    auto *laterButton = new QPushButton(QStringLiteral("稍后支付"), this);
    laterButton->setFlat(true);

    auto *billLayout = new QVBoxLayout;
    billLayout->addWidget(m_energyLabel);
    billLayout->addWidget(m_durationLabel);
    billLayout->addWidget(m_priceLabel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addStretch(1);
    layout->addWidget(title);
    layout->addSpacing(12);
    layout->addLayout(billLayout);
    layout->addSpacing(12);
    layout->addWidget(m_totalLabel);
    layout->addStretch(1);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_payButton);
    layout->addWidget(m_topUpButton);
    layout->addWidget(laterButton);

    connect(m_payButton, &QPushButton::clicked, this, &SettleView::settle);
    connect(m_topUpButton, &QPushButton::clicked, this, &SettleView::quickTopUp);
    connect(laterButton, &QPushButton::clicked, this, &SettleView::dismissed);
}

void SettleView::open(const Order &order)
{
    m_order = order;
    m_energyLabel->setText(QStringLiteral("充电量：%1 度").arg(order.energyKwh, 0, 'f', 1));
    m_durationLabel->setText(QStringLiteral("充电时长：%1 分钟").arg(order.durationMinutes));
    m_priceLabel->setText(QStringLiteral("单价：%1 元/度").arg(fenToYuan(order.unitPriceFenPerKwh)));
    m_totalLabel->setText(QStringLiteral("合计 %1 元").arg(fenToYuan(order.amountFen)));
    m_messageLabel->hide();
    m_topUpButton->hide();
    m_payButton->setEnabled(true);
}

void SettleView::settle()
{
    m_payButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/settle").arg(m_order.id),
               QJsonObject{{QLatin1String("paymentMethod"), QStringLiteral("wallet")}},
               [this](const QJsonValue &, const QJsonObject &) { emit settled(); },
               [this](const ApiError &error) {
                   m_payButton->setEnabled(true);
                   if (error.code == QLatin1String("INSUFFICIENT_BALANCE")) {
                       m_messageLabel->setText(QStringLiteral("钱包余额不足，请先充值"));
                       m_messageLabel->show();
                       m_topUpButton->show();
                       return;
                   }
                   QMessageBox::warning(this, QStringLiteral("结算失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}

void SettleView::quickTopUp()
{
    m_topUpButton->setEnabled(false);
    m_api.post(QStringLiteral("/me/wallet/topups"),
               QJsonObject{{QLatin1String("amountFen"), kQuickTopUpFen}, {QLatin1String("note"), QStringLiteral("快捷充值")}},
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_topUpButton->setEnabled(true);
                   const qlonglong balance = data.toObject().value(QLatin1String("balanceAfterFen")).toInteger();
                   m_messageLabel->setStyleSheet(QStringLiteral("color: #2e9e5b;"));
                   m_messageLabel->setText(QStringLiteral("充值成功，当前余额 %1 元").arg(fenToYuan(balance)));
                   m_topUpButton->hide();
               },
               [this](const ApiError &error) {
                   m_topUpButton->setEnabled(true);
                   m_messageLabel->setStyleSheet(QStringLiteral("color: #d33;"));
                   m_messageLabel->setText(error.message.isEmpty() ? error.code : error.message);
                   m_messageLabel->show();
               });
}
