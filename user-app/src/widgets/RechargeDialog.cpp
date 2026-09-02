#include "RechargeDialog.h"

#include "common/Format.h"

#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace {
const QRegularExpression kAmountPattern{QLatin1String("\\d{1,5}(\\.\\d{0,2})?")};
}

RechargeDialog::RechargeDialog(ApiClient &api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("账户充值"));
    setFixedWidth(300);

    auto *title = new QLabel(QStringLiteral("账户充值"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));

    auto *quickBox = new QGridLayout;
    const QList<int> quickAmounts = {50, 100, 200};
    for (int i = 0; i < quickAmounts.size(); ++i) {
        auto *button = new QPushButton(QStringLiteral("￥%1").arg(quickAmounts.at(i)), this);
        const int amount = quickAmounts.at(i);
        connect(button, &QPushButton::clicked, this, [this, amount] {
            m_amountEdit->setText(QString::number(amount));
        });
        quickBox->addWidget(button, i / 3, i % 3);
    }

    m_amountEdit = new QLineEdit(this);
    m_amountEdit->setPlaceholderText(QStringLiteral("自定义金额（元）"));
    m_amountEdit->setValidator(new QRegularExpressionValidator(kAmountPattern, this));

    m_payButton = new QPushButton(QStringLiteral("模拟支付"), this);
    m_payButton->setObjectName(QStringLiteral("primaryButton"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addWidget(title);
    layout->addSpacing(12);
    layout->addLayout(quickBox);
    layout->addWidget(m_amountEdit);
    layout->addWidget(m_payButton);

    connect(m_payButton, &QPushButton::clicked, this, &RechargeDialog::pay);
}

void RechargeDialog::pay()
{
    bool ok = false;
    const double yuan = m_amountEdit->text().toDouble(&ok);
    if (!ok || yuan < 0.01) {
        QMessageBox::warning(this, QStringLiteral("充值"), QStringLiteral("请输入有效金额"));
        return;
    }

    const qlonglong amountFen = qRound64(yuan * 100);
    m_payButton->setEnabled(false);
    QJsonObject body;
    body.insert(QLatin1String("amountFen"), amountFen);
    body.insert(QLatin1String("note"), QStringLiteral("模拟充值"));
    m_api.post(QStringLiteral("/me/wallet/topups"), body,
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_payButton->setEnabled(true);
                   const qlonglong balance = data.toObject().value(QLatin1String("balanceAfterFen")).toInteger();
                   QMessageBox::information(this, QStringLiteral("支付成功"), QStringLiteral("微信/支付宝支付成功"));
                   emit succeeded(balance);
                   accept();
               },
               [this](const ApiError &error) {
                   m_payButton->setEnabled(true);
                   QMessageBox::warning(this, QStringLiteral("充值失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}
