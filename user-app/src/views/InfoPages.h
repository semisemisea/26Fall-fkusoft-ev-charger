#pragma once

#include "api/ApiClient.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

class OrderHistoryView : public QWidget
{
    Q_OBJECT

public:
    explicit OrderHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

class TransactionsView : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionsView(ApiClient &api, QWidget *parent = nullptr);

signals:
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

class CarView : public QWidget
{
    Q_OBJECT

public:
    explicit CarView(QWidget *parent = nullptr);

signals:
    void backRequested();
};

class AboutView : public QWidget
{
    Q_OBJECT

public:
    explicit AboutView(QWidget *parent = nullptr);

signals:
    void backRequested();
};
