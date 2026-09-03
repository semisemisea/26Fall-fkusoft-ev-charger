#include "InfoPages.h"

#include "common/Format.h"
#include "models/Order.h"
#include "models/Reservation.h"
#include "widgets/BackButton.h"
#include "widgets/Spinner.h"

#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
QLabel *makePageShell(const QString &title, const std::function<void()> &onBack,
                      QVBoxLayout **listLayout, Spinner **spinnerOut, QWidget *parent)
{
    auto *backButton = new BackButton(parent);
    auto *titleLabel = new QLabel(title, parent);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));

    auto *statusLabel = new QLabel(parent);
    statusLabel->setObjectName(QStringLiteral("muted"));
    statusLabel->hide();

    auto *spinner = new Spinner(parent);
    spinner->hide();
    *spinnerOut = spinner;

    auto *statusRow = new QWidget(parent);
    auto *statusRowLayout = new QHBoxLayout(statusRow);
    statusRowLayout->setContentsMargins(0, 0, 0, 0);
    statusRowLayout->setSpacing(8);
    statusRowLayout->addStretch();
    statusRowLayout->addWidget(spinner);
    statusRowLayout->addWidget(statusLabel);
    statusRowLayout->addStretch();

    auto *container = new QWidget(parent);
    *listLayout = new QVBoxLayout(container);
    (*listLayout)->setContentsMargins(0, 0, 0, 0);
    (*listLayout)->setSpacing(10);
    (*listLayout)->addWidget(statusRow);
    (*listLayout)->addStretch();

    auto *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(container);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(backButton);
    headerRow->addStretch();
    headerRow->addWidget(titleLabel);
    headerRow->addStretch();

    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addLayout(headerRow);
    layout->addWidget(scrollArea);

    QObject::connect(backButton, &QPushButton::clicked, parent, onBack);
    return statusLabel;
}

void clearCards(QVBoxLayout *listLayout)
{
    while (listLayout->count() > 2) {
        QLayoutItem *item = listLayout->takeAt(1);
        item->widget()->deleteLater();
        delete item;
    }
}
} // namespace

OrderHistoryView::OrderHistoryView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    QVBoxLayout *listLayout = nullptr;
    m_statusLabel = makePageShell(QStringLiteral("历史充电订单"), [this] { emit backRequested(); },
                                  &listLayout, &m_spinner, this);
    m_listLayout = listLayout;
}

void OrderHistoryView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    load();
}

void OrderHistoryView::load()
{
    m_spinner->show();
    m_statusLabel->hide();
    m_api.get(QStringLiteral("/orders?pageSize=50"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray orders = data.toArray();
                  if (orders.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无历史订单"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : orders) {
                      const Order order = Order::fromJson(value.toObject());

                      const QString statusColor = order.status == QLatin1String("settled")
                                                      ? QStringLiteral("#2e9e5b")
                                                      : (order.status == QLatin1String("awaiting_payment")
                                                             ? QStringLiteral("#e67e22")
                                                             : QStringLiteral("#2a6fdb"));

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *nameLabel = new QLabel(order.stationName, card);
                      nameLabel->setObjectName(QStringLiteral("cardTitle"));
                      auto *statusLabel = new QLabel(Order::statusLabel(order.status), card);
                      statusLabel->setObjectName(QStringLiteral("statusStrong"));
                      statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor));
                      auto *detailLabel = new QLabel(
                          QStringLiteral("电桩 %1 · %2 · %3 分钟")
                              .arg(order.chargerCode,
                                   QString(order.startedAt).replace(QLatin1Char('T'), QLatin1Char(' ')).left(16))
                              .arg(order.durationMinutes),
                          card);
                      detailLabel->setObjectName(QStringLiteral("meta"));
                      auto *amountLabel = new QLabel(
                          QStringLiteral("%1 度 · ￥%2")
                              .arg(order.energyKwh, 0, 'f', 1)
                              .arg(fenToYuan(order.amountFen)),
                          card);
                      amountLabel->setObjectName(QStringLiteral("strong"));
                      amountLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(nameLabel, 0, 0);
                      grid->addWidget(statusLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(detailLabel, 1, 0);
                      grid->addWidget(amountLabel, 1, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  }
              },
              [this](const ApiError &error) {
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}

TransactionsView::TransactionsView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    QVBoxLayout *listLayout = nullptr;
    m_statusLabel = makePageShell(QStringLiteral("钱包流水"), [this] { emit backRequested(); },
                                  &listLayout, &m_spinner, this);
    m_listLayout = listLayout;
}

void TransactionsView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    load();
}

void TransactionsView::load()
{
    m_spinner->show();
    m_statusLabel->hide();
    m_api.get(QStringLiteral("/me/wallet/transactions"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray transactions = data.toArray();
                  if (transactions.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无流水记录"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : transactions) {
                      const QJsonObject object = value.toObject();
                      QString type;
                      const QString typeCode = object.value(QLatin1String("type")).toString();
                      if (typeCode == QLatin1String("top_up")) {
                          type = QStringLiteral("充值");
                      } else if (typeCode == QLatin1String("charge_debit")) {
                          type = QStringLiteral("充电扣款");
                      } else if (typeCode == QLatin1String("refund")) {
                          type = QStringLiteral("退款");
                      } else {
                          type = QStringLiteral("调整");
                      }
                      const qlonglong amount = object.value(QLatin1String("amountFen")).toInteger();

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *typeLabel = new QLabel(type, card);
                      typeLabel->setObjectName(QStringLiteral("strong"));
                      auto *timeLabel = new QLabel(
                          QString(object.value(QLatin1String("createdAt")).toString())
                              .replace(QLatin1Char('T'), QLatin1Char(' '))
                              .left(16),
                          card);
                      timeLabel->setObjectName(QStringLiteral("meta"));
                      auto *amountLabel = new QLabel(
                          QStringLiteral("%1￥%2")
                              .arg(amount >= 0 ? QStringLiteral("+") : QStringLiteral("-"),
                                   fenToYuan(qAbs(amount))),
                          card);
                      amountLabel->setObjectName(QStringLiteral("strong"));
                      amountLabel->setStyleSheet(
                          QStringLiteral("color: %1;")
                              .arg(amount >= 0 ? QStringLiteral("#2e9e5b") : QStringLiteral("#e74c3c")));
                      amountLabel->setAlignment(Qt::AlignRight);
                      auto *balanceLabel = new QLabel(
                          QStringLiteral("余额 ￥%1")
                              .arg(fenToYuan(object.value(QLatin1String("balanceAfterFen")).toInteger())),
                          card);
                      balanceLabel->setObjectName(QStringLiteral("meta"));
                      balanceLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(typeLabel, 0, 0);
                      grid->addWidget(amountLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(timeLabel, 1, 0);
                      grid->addWidget(balanceLabel, 1, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  }
              },
              [this](const ApiError &error) {
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}

ReservationHistoryView::ReservationHistoryView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    QVBoxLayout *listLayout = nullptr;
    m_statusLabel = makePageShell(QStringLiteral("我的预约记录"), [this] { emit backRequested(); },
                                  &listLayout, &m_spinner, this);
    m_listLayout = listLayout;
}

void ReservationHistoryView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    load();
}

void ReservationHistoryView::load()
{
    m_spinner->show();
    m_statusLabel->hide();
    m_api.get(QStringLiteral("/reservations"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_spinner->hide();
                  clearCards(m_listLayout);
                  const QJsonArray reservations = data.toArray();
                  if (reservations.isEmpty()) {
                      m_statusLabel->setText(QStringLiteral("暂无预约记录"));
                      m_statusLabel->show();
                      return;
                  }
                  for (const QJsonValue &value : reservations) {
                      const Reservation reservation = Reservation::fromJson(value.toObject());

                      const QString statusColor = reservation.status == QLatin1String("active")
                                                      ? QStringLiteral("#1d5cff")
                                                      : (reservation.status == QLatin1String("used")
                                                             ? QStringLiteral("#2e9e5b")
                                                             : (reservation.status == QLatin1String("expired")
                                                                    ? QStringLiteral("#e67e22")
                                                                    : QStringLiteral("#8a8f99")));

                      auto *card = new QFrame(this);
                      card->setObjectName(QStringLiteral("infoCard"));
                      auto *nameLabel = new QLabel(reservation.stationName, card);
                      nameLabel->setObjectName(QStringLiteral("cardTitle"));
                      auto *statusLabel = new QLabel(Reservation::statusLabel(reservation.status), card);
                      statusLabel->setObjectName(QStringLiteral("statusStrong"));
                      statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor));
                      auto *detailLabel = new QLabel(
                          QStringLiteral("电桩 %1 · %2")
                              .arg(reservation.chargerCode,
                                   QString(reservation.startAt)
                                       .replace(QLatin1Char('T'), QLatin1Char(' '))
                                       .left(16)),
                          card);
                      detailLabel->setObjectName(QStringLiteral("meta"));
                      auto *expireLabel = new QLabel(
                          QStringLiteral("到期 %1")
                              .arg(reservation.expiresAt.toString(QStringLiteral("MM-dd hh:mm"))),
                          card);
                      expireLabel->setObjectName(QStringLiteral("meta"));
                      expireLabel->setAlignment(Qt::AlignRight);

                      auto *grid = new QGridLayout(card);
                      grid->setContentsMargins(14, 10, 14, 10);
                      grid->setVerticalSpacing(4);
                      grid->addWidget(nameLabel, 0, 0);
                      grid->addWidget(statusLabel, 0, 1, Qt::AlignRight);
                      grid->addWidget(detailLabel, 1, 0);
                      grid->addWidget(expireLabel, 1, 1, Qt::AlignRight);

                      m_listLayout->insertWidget(m_listLayout->count() - 1, card);
                  }
              },
              [this](const ApiError &error) {
                  m_spinner->hide();
                  m_statusLabel->setText(error.message.isEmpty() ? error.code : error.message);
                  m_statusLabel->show();
              });
}

CarView::CarView(QWidget *parent)
    : QWidget(parent)
{
    auto *backButton = new BackButton(this);
    connect(backButton, &QPushButton::clicked, this, &CarView::backRequested);
    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(backButton);
    headerRow->addStretch();

    auto *iconLabel = new QLabel(QStringLiteral("🚗"), this);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setObjectName(QStringLiteral("heroIcon"));

    auto *nameLabel = new QLabel(QStringLiteral("比亚迪 · 汉 EV"), this);
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setObjectName(QStringLiteral("heroTitle"));

    auto *detailLabel = new QLabel(QStringLiteral("电池容量 60.5 kWh · 支持快充\n车牌 辽B·D88888"), this);
    detailLabel->setAlignment(Qt::AlignCenter);
    detailLabel->setObjectName(QStringLiteral("muted"));

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("profileCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 32, 24, 32);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(iconLabel);
    cardLayout->addWidget(nameLabel);
    cardLayout->addWidget(detailLabel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(headerRow);
    layout->addWidget(card);
    layout->addStretch();
}

AboutView::AboutView(QWidget *parent)
    : QWidget(parent)
{
    auto *backButton = new BackButton(this);
    connect(backButton, &QPushButton::clicked, this, &AboutView::backRequested);
    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(backButton);
    headerRow->addStretch();

    auto *iconLabel = new QLabel(QStringLiteral("⚡"), this);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setObjectName(QStringLiteral("heroIconSmall"));

    auto *nameLabel = new QLabel(QStringLiteral("智能充电系统"), this);
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setObjectName(QStringLiteral("heroTitle"));

    auto *versionLabel = new QLabel(QStringLiteral("版本 1.0.0"), this);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setObjectName(QStringLiteral("muted"));

    auto *detailLabel = new QLabel(QStringLiteral("电动汽车充电桩应用管理平台 · 用户端\n基于 Qt 6 构建"), this);
    detailLabel->setAlignment(Qt::AlignCenter);
    detailLabel->setObjectName(QStringLiteral("muted"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(headerRow);
    layout->addStretch(2);
    layout->addWidget(iconLabel);
    layout->addWidget(nameLabel);
    layout->addWidget(versionLabel);
    layout->addSpacing(12);
    layout->addWidget(detailLabel);
    layout->addStretch(3);
}
