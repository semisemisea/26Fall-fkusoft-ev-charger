#pragma once

#include "api/ApiClient.h"

#include <QWidget>

class QLabel;
class Spinner;
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
    Spinner *m_spinner = nullptr;
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
    Spinner *m_spinner = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
};

class ReservationHistoryView : public QWidget
{
    Q_OBJECT

public:
    explicit ReservationHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void load();

    ApiClient &m_api;
    QLabel *m_statusLabel = nullptr;
    Spinner *m_spinner = nullptr;
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
