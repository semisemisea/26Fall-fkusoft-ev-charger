#include "SettleView.h"

#include "common/Format.h"
#include "common/Theme.h"
#include "widgets/Toast.h"
#include "widgets/RechargeDialog.h"
#include "widgets/ScaleButton.h"
#include "widgets/AppIcons.h"

#include <QFrame>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// 构造：搭建账单卡、余额与支付 / 充值 / 稍后支付 / 返回首页按钮
SettleView::SettleView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    m_titleLabel = new QLabel(QStringLiteral("订单结算"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setObjectName(QStringLiteral("heroTitle"));

    auto *receipt = new QFrame(this);
    receipt->setObjectName(QStringLiteral("receiptCard"));
    m_stationLabel = new QLabel(receipt);
    m_stationLabel->setObjectName(QStringLiteral("cardHeading"));
    m_timeLabel = new QLabel(receipt);
    m_timeLabel->setObjectName(QStringLiteral("meta"));
    m_energyLabel = new QLabel(receipt);
    m_durationLabel = new QLabel(receipt);
    m_priceLabel = new QLabel(receipt);
    m_totalLabel = new QLabel(receipt);
    m_totalLabel->setAlignment(Qt::AlignCenter);
    m_totalLabel->setObjectName(QStringLiteral("totalAmount"));

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
    m_balanceLabel->setObjectName(QStringLiteral("muted"));

    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setObjectName(QStringLiteral("settleMessage"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->hide();

    m_payButton = new ScaleButton(QStringLiteral("确认支付（钱包）"), this);
    m_payButton->setObjectName(QStringLiteral("settlePayButton"));

    m_topUpButton = new ScaleButton(QStringLiteral("余额不足，去充值"), this);
    m_topUpButton->setObjectName(QStringLiteral("warnButton"));
    m_topUpButton->hide();

    m_laterButton = new QPushButton(QStringLiteral("稍后支付"), this);
    m_laterButton->setFlat(true);

    m_homeButton = new ScaleButton(QStringLiteral("返回首页"), this);
    m_homeButton->setObjectName(QStringLiteral("settleHomeButton"));
    m_homeButton->hide();

    // 结算页容器
    m_settleContainer = new QWidget(this);
    auto *settleLayout = new QVBoxLayout(m_settleContainer);
    settleLayout->setContentsMargins(16, 16, 16, 16);
    settleLayout->addStretch(1);
    settleLayout->addWidget(m_titleLabel);
    settleLayout->addSpacing(12);
    settleLayout->addWidget(receipt);
    settleLayout->addSpacing(10);
    settleLayout->addWidget(m_balanceLabel);
    settleLayout->addStretch(1);
    settleLayout->addWidget(m_messageLabel);
    settleLayout->addWidget(m_payButton);
    settleLayout->addWidget(m_topUpButton);
    settleLayout->addWidget(m_laterButton);
    settleLayout->addWidget(m_homeButton);

    // 支付成功页容器
    m_successContainer = new QWidget(this);
    auto *successLayout = new QVBoxLayout(m_successContainer);
    successLayout->setContentsMargins(16, 0, 16, 0);
    successLayout->setSpacing(10);
    successLayout->addStretch();

    m_successIconLabel = new QLabel(m_successContainer);
    m_successIconLabel->setAlignment(Qt::AlignCenter);
    successLayout->addWidget(m_successIconLabel);

    m_successAmountLabel = new QLabel(m_successContainer);
    m_successAmountLabel->setAlignment(Qt::AlignCenter);
    m_successAmountLabel->setObjectName(QStringLiteral("settleSuccessAmount"));
    successLayout->addWidget(m_successAmountLabel);

    m_successTitleLabel = new QLabel(QStringLiteral("支付成功"), m_successContainer);
    m_successTitleLabel->setAlignment(Qt::AlignCenter);
    m_successTitleLabel->setObjectName(QStringLiteral("settleSuccessTitle"));
    successLayout->addWidget(m_successTitleLabel);

    m_successSubtitleLabel = new QLabel(QStringLiteral("感谢您的使用"), m_successContainer);
    m_successSubtitleLabel->setAlignment(Qt::AlignCenter);
    m_successSubtitleLabel->setObjectName(QStringLiteral("settleSuccessSubtitle"));
    successLayout->addWidget(m_successSubtitleLabel);

    successLayout->addSpacing(12);

    m_successHomeButton = new QPushButton(QStringLiteral("返回首页"), m_successContainer);
    m_successHomeButton->setObjectName(QStringLiteral("settleBackHomeButton"));
    m_successHomeButton->setFixedHeight(54);
    successLayout->addWidget(m_successHomeButton, 0, Qt::AlignCenter);

    successLayout->addStretch();
    m_successContainer->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_settleContainer);
    layout->addWidget(m_successContainer);

    connect(m_payButton, &QPushButton::clicked, this, &SettleView::settle);
    connect(m_topUpButton, &QPushButton::clicked, this, &SettleView::openRecharge);
    connect(m_laterButton, &QPushButton::clicked, this, &SettleView::dismissed);
    connect(m_homeButton, &QPushButton::clicked, this, &SettleView::returnHomeRequested);
    connect(m_successHomeButton, &QPushButton::clicked, this, &SettleView::returnHomeRequested);
    connect(&m_session, &Session::userChanged, this, &SettleView::refreshBalance);
}

// 填充账单明细，重置按钮为待支付状态并刷新余额显示
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
    m_successContainer->hide();
    m_settleContainer->show();
}

// 钱包支付结算；余额不足（INSUFFICIENT_BALANCE）时提示并显示“去充值”按钮
void SettleView::settle()
{
    m_payButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/settle").arg(m_order.id),
               QJsonObject{{QLatin1String("paymentMethod"), QStringLiteral("wallet")}},
               [this](const QJsonValue &, const QJsonObject &) {
                   m_settleContainer->hide();
                   m_successIconLabel->setPixmap(AppIcons::successCheck(width() - 32, 240));
                   m_successAmountLabel->setText(QStringLiteral("￥%1").arg(fenToYuan(m_order.amountFen)));
                   m_successContainer->show();
                   emit settled();
               },
               [this](const ApiError &error) {
                   m_payButton->setEnabled(true);
                   if (error.code == QLatin1String("INSUFFICIENT_BALANCE")) {
                       m_messageLabel->setText(QStringLiteral("钱包余额不足，请先充值"));
                       m_messageLabel->show();
                       m_topUpButton->show();
                       return;
                   }
                   Toast::error(this, error.message.isEmpty() ? error.code : error.message);
               });
}

// 打开充值对话框，成功后更新会话余额并恢复支付入口
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

// 显示会话中的最新钱包余额
void SettleView::refreshBalance()
{
    m_balanceLabel->setText(QStringLiteral("当前余额：￥%1").arg(fenToYuan(m_session.user().walletBalanceFen)));
}

// ISO 时间串转 "yyyy-MM-dd hh:mm" 显示格式
QString SettleView::formatTime(QString isoTime)
{
    return isoTime.replace(QLatin1Char('T'), QLatin1Char(' ')).left(16);
}
