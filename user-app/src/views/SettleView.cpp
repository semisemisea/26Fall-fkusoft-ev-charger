#include "SettleView.h"

#include "common/Format.h"
#include "widgets/RechargeDialog.h"

#include <QFrame>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

SettleView::SettleView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    m_titleLabel = new QLabel(QStringLiteral("订单结算"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));

    auto *receipt = new QFrame(this);
    receipt->setStyleSheet(QStringLiteral(
        "QFrame { background: white; border: 1px dashed #d5d9e0; border-radius: 12px; }"
        "QLabel { border: none; }"));
    m_stationLabel = new QLabel(receipt);
    m_stationLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    m_timeLabel = new QLabel(receipt);
    m_timeLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    m_energyLabel = new QLabel(receipt);
    m_durationLabel = new QLabel(receipt);
    m_priceLabel = new QLabel(receipt);
    m_totalLabel = new QLabel(receipt);
    m_totalLabel->setAlignment(Qt::AlignCenter);
    m_totalLabel->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold; color: #e74c3c;"));

    auto *receiptLayout = new QVBoxLayout(receipt);
    receiptLayout->setContentsMargins(16, 14, 16, 14);
    receiptLayout->setSpacing(8);
    receiptLayout->addWidget(m_stationLabel);
    receiptLayout->addWidget(m_timeLabel);
    receiptLayout->addWidget(m_energyLabel);
    receiptLayout->addWidget(m_durationLabel);
    receiptLayout->addWidget(m_priceLabel);
    receiptLayout->addSpacing(6);
    receiptLayout->addWidget(m_totalLabel);

    m_balanceLabel = new QLabel(this);
    m_balanceLabel->setAlignment(Qt::AlignCenter);
    m_balanceLabel->setStyleSheet(QStringLiteral("color: #555;"));

    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setStyleSheet(QStringLiteral("color: #e74c3c;"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->hide();

    m_payButton = new QPushButton(QStringLiteral("确认支付（钱包）"), this);
    m_payButton->setObjectName(QStringLiteral("primaryButton"));

    m_topUpButton = new QPushButton(QStringLiteral("余额不足，去充值"), this);
    m_topUpButton->setObjectName(QStringLiteral("warnButton"));
    m_topUpButton->hide();

    m_laterButton = new QPushButton(QStringLiteral("稍后支付"), this);
    m_laterButton->setFlat(true);

    m_homeButton = new QPushButton(QStringLiteral("返回首页"), this);
    m_homeButton->setObjectName(QStringLiteral("primaryButton"));
    m_homeButton->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addStretch(1);
    layout->addWidget(m_titleLabel);
    layout->addSpacing(12);
    layout->addWidget(receipt);
    layout->addSpacing(10);
    layout->addWidget(m_balanceLabel);
    layout->addStretch(1);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_payButton);
    layout->addWidget(m_topUpButton);
    layout->addWidget(m_laterButton);
    layout->addWidget(m_homeButton);

    connect(m_payButton, &QPushButton::clicked, this, &SettleView::settle);
    connect(m_topUpButton, &QPushButton::clicked, this, &SettleView::openRecharge);
    connect(m_laterButton, &QPushButton::clicked, this, &SettleView::dismissed);
    connect(m_homeButton, &QPushButton::clicked, this, &SettleView::returnHomeRequested);
    connect(&m_session, &Session::userChanged, this, &SettleView::refreshBalance);
}

void SettleView::open(const Order &order)
{
    m_order = order;
    m_titleLabel->setText(QStringLiteral("订单已生成"));
    m_stationLabel->setText(QStringLiteral("%1 · 电桩 %2").arg(order.stationName, order.chargerCode));
    m_timeLabel->setText(QStringLiteral("%1 ~ %2")
                             .arg(formatTime(order.startedAt),
                                  order.endedAt.isEmpty() ? QStringLiteral("—") : formatTime(order.endedAt)));
    m_energyLabel->setText(QStringLiteral("充电量：%1 度").arg(order.energyKwh, 0, 'f', 1));
    m_durationLabel->setText(QStringLiteral("充电时长：%1 分钟").arg(order.durationMinutes));
    m_priceLabel->setText(QStringLiteral("单价：￥%1/度").arg(fenToYuan(order.unitPriceFenPerKwh)));
    m_totalLabel->setText(QStringLiteral("合计 ￥%1").arg(fenToYuan(order.amountFen)));
    refreshBalance();
    m_messageLabel->hide();
    m_topUpButton->hide();
    m_homeButton->hide();
    m_payButton->show();
    m_laterButton->show();
    m_payButton->setEnabled(true);
}

void SettleView::settle()
{
    m_payButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/settle").arg(m_order.id),
               QJsonObject{{QLatin1String("paymentMethod"), QStringLiteral("wallet")}},
               [this](const QJsonValue &, const QJsonObject &) {
                   m_titleLabel->setText(QStringLiteral("✅ 支付完成"));
                   m_messageLabel->setStyleSheet(QStringLiteral("color: #2e9e5b;"));
                   m_messageLabel->setText(QStringLiteral("订单已支付，感谢使用"));
                   m_messageLabel->show();
                   m_payButton->hide();
                   m_topUpButton->hide();
                   m_laterButton->hide();
                   m_homeButton->show();
                   emit settled();
               },
               [this](const ApiError &error) {
                   m_payButton->setEnabled(true);
                   if (error.code == QLatin1String("INSUFFICIENT_BALANCE")) {
                       m_messageLabel->setStyleSheet(QStringLiteral("color: #e74c3c;"));
                       m_messageLabel->setText(QStringLiteral("钱包余额不足，请先充值"));
                       m_messageLabel->show();
                       m_topUpButton->show();
                       return;
                   }
                   QMessageBox::warning(this, QStringLiteral("结算失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}

void SettleView::openRecharge()
{
    auto *dialog = new RechargeDialog(m_api, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &RechargeDialog::succeeded, this, [this](qlonglong balance) {
        m_session.updateBalance(balance);
        m_messageLabel->hide();
        m_topUpButton->hide();
    });
    dialog->open();
}

void SettleView::refreshBalance()
{
    m_balanceLabel->setText(QStringLiteral("当前余额：￥%1").arg(fenToYuan(m_session.user().walletBalanceFen)));
}

QString SettleView::formatTime(QString isoTime)
{
    return isoTime.replace(QLatin1Char('T'), QLatin1Char(' ')).left(16);
}
