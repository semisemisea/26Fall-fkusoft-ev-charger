#pragma once

#include "api/ApiClient.h"

#include <QDialog>
#include <QtGlobal>

class QLineEdit;
class QPushButton;
class QPropertyAnimation;

class RechargeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RechargeDialog(ApiClient &api, QWidget *parent = nullptr);

signals:
    void succeeded(qlonglong balanceFen);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void pay();

    ApiClient &m_api;
    QLineEdit *m_amountEdit = nullptr;
    QPushButton *m_payButton = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
};
