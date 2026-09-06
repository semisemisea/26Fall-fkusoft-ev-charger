#pragma once

#include "api/ApiClient.h"

#include <QDialog>
#include <QtGlobal>

class QLineEdit;
class QPushButton;
class QPropertyAnimation;

// 充值弹窗：POST /me/wallet/topups 模拟充值（立即成功），成功发 succeeded(新余额)
class RechargeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RechargeDialog(ApiClient &api, QWidget *parent = nullptr);

signals:
    // 充值成功，携带最新余额（分）
    void succeeded(qlonglong balanceFen);

protected:
    // 首次显示时淡入
    void showEvent(QShowEvent *event) override;

private:
    // 校验金额并发起充值请求
    void pay();

    ApiClient &m_api;
    QLineEdit *m_amountEdit = nullptr;
    QPushButton *m_payButton = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
};
