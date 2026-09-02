#include "ProfileView.h"

#include "common/Format.h"
#include "models/User.h"

#include <QEvent>
#include <QFileDialog>
#include <QHttpMultiPart>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkRequest>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const QLatin1String kDefaultAvatarStyle{
    "QLabel { background: #ccc; border-radius: 40px; color: white; font-size: 40px; }"};
const QLatin1String kAvatarPixmapStyle{"QLabel { border-radius: 40px; }"};
}

ProfileView::ProfileView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    auto *backButton = new QPushButton(QStringLiteral("← 返回"), this);
    auto *title = new QLabel(QStringLiteral("个人中心"), this);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));

    m_avatarLabel = new QLabel(QStringLiteral("👤"), this);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    m_avatarLabel->setFixedSize(80, 80);
    m_avatarLabel->setStyleSheet(kDefaultAvatarStyle);
    m_avatarLabel->setCursor(Qt::PointingHandCursor);
    m_avatarLabel->installEventFilter(this);

    auto *avatarHint = new QLabel(QStringLiteral("点击更换头像"), this);
    avatarHint->setStyleSheet(QStringLiteral("color: #999; font-size: 11px;"));
    avatarHint->setAlignment(Qt::AlignCenter);

    m_nicknameLabel = new QLabel(this);
    m_nicknameLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
    auto *nicknameButton = new QPushButton(QStringLiteral("修改昵称"), this);

    m_phoneLabel = new QLabel(this);
    m_phoneLabel->setStyleSheet(QStringLiteral("color: #999;"));

    m_balanceLabel = new QLabel(this);
    m_balanceLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: bold; color: #2a6fdb;"));
    auto *topUpButton = new QPushButton(QStringLiteral("余额充值"), this);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->hide();

    auto *transactionsTitle = new QLabel(QStringLiteral("钱包流水"), this);
    transactionsTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    m_transactionList = new QListWidget(this);
    m_transactionList->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid #ddd; border-radius: 8px; }"));

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(backButton);
    headerRow->addStretch();
    headerRow->addWidget(title);
    headerRow->addStretch();

    auto *profileRow = new QHBoxLayout;
    auto *avatarColumn = new QVBoxLayout;
    avatarColumn->addWidget(m_avatarLabel);
    avatarColumn->addWidget(avatarHint);
    auto *infoColumn = new QVBoxLayout;
    infoColumn->addWidget(m_nicknameLabel);
    infoColumn->addWidget(nicknameButton);
    infoColumn->addWidget(m_phoneLabel);
    infoColumn->addStretch();
    profileRow->addLayout(avatarColumn);
    profileRow->addSpacing(16);
    profileRow->addLayout(infoColumn, 1);

    auto *balanceRow = new QHBoxLayout;
    balanceRow->addWidget(m_balanceLabel);
    balanceRow->addStretch();
    balanceRow->addWidget(topUpButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(headerRow);
    layout->addSpacing(8);
    layout->addLayout(profileRow);
    layout->addSpacing(8);
    layout->addLayout(balanceRow);
    layout->addWidget(m_messageLabel);
    layout->addSpacing(8);
    layout->addWidget(transactionsTitle);
    layout->addWidget(m_transactionList, 1);

    connect(backButton, &QPushButton::clicked, this, &ProfileView::backRequested);
    connect(nicknameButton, &QPushButton::clicked, this, &ProfileView::changeNickname);
    connect(topUpButton, &QPushButton::clicked, this, &ProfileView::topUp);
    connect(&m_session, &Session::userChanged, this, &ProfileView::refreshProfile);
}

void ProfileView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshProfile();
    loadTransactions();
}

bool ProfileView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_avatarLabel && event->type() == QEvent::MouseButtonRelease) {
        changeAvatar();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ProfileView::refreshProfile()
{
    const User &user = m_session.user();
    m_nicknameLabel->setText(user.nickname);
    m_phoneLabel->setText(user.phone);
    m_balanceLabel->setText(QStringLiteral("余额 %1 元").arg(fenToYuan(user.walletBalanceFen)));
    if (user.avatarUrl != m_loadedAvatarUrl) {
        loadAvatar();
    }
}

void ProfileView::loadAvatar()
{
    m_loadedAvatarUrl = m_session.user().avatarUrl;
    if (m_loadedAvatarUrl.isEmpty()) {
        m_avatarLabel->setPixmap(QPixmap());
        m_avatarLabel->setText(QStringLiteral("👤"));
        m_avatarLabel->setStyleSheet(kDefaultAvatarStyle);
        return;
    }
    m_api.download(QUrl(m_loadedAvatarUrl),
                   [this](const QByteArray &payload) {
                       QPixmap pixmap;
                       if (!pixmap.loadFromData(payload)) {
                           return;
                       }
                       m_avatarLabel->setText(QString());
                       m_avatarLabel->setStyleSheet(kAvatarPixmapStyle);
                       m_avatarLabel->setPixmap(pixmap.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                   },
                   [this](const ApiError &) {
                       m_avatarLabel->setText(QStringLiteral("👤"));
                       m_avatarLabel->setStyleSheet(kDefaultAvatarStyle);
                   });
}

void ProfileView::changeAvatar()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), QString(),
                                                      QStringLiteral("图片 (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) {
        return;
    }

    auto *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        QMessageBox::warning(this, QStringLiteral("上传失败"), QStringLiteral("无法读取所选文件"));
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral(R"(form-data; name="file"; filename="avatar")")));
    filePart.setBodyDevice(file);
    multiPart->append(filePart);
    file->setParent(multiPart);

    m_api.upload(QStringLiteral("/me/avatar"), multiPart,
                 [this](const QJsonValue &data, const QJsonObject &) {
                     User user = m_session.user();
                     user.avatarUrl = data.toObject().value(QLatin1String("avatarUrl")).toString();
                     m_session.updateUser(user);
                 },
                 [this](const ApiError &error) {
                     QMessageBox::warning(this, QStringLiteral("上传失败"),
                                          error.message.isEmpty() ? error.code : error.message);
                 });
}

void ProfileView::changeNickname()
{
    bool ok = false;
    const QString nickname = QInputDialog::getText(this, QStringLiteral("修改昵称"), QStringLiteral("新昵称（1-30 字符）"),
                                                   QLineEdit::Normal, m_session.user().nickname, &ok);
    if (!ok || nickname.trimmed().isEmpty() || nickname == m_session.user().nickname) {
        return;
    }

    QJsonObject body;
    body.insert(QLatin1String("nickname"), nickname.trimmed());
    m_api.patch(QStringLiteral("/me"), body,
                [this](const QJsonValue &data, const QJsonObject &) {
                    m_session.updateUser(User::fromJson(data.toObject()));
                },
                [this](const ApiError &error) {
                    QMessageBox::warning(this, QStringLiteral("修改失败"),
                                         error.message.isEmpty() ? error.code : error.message);
                });
}

void ProfileView::topUp()
{
    bool ok = false;
    const int yuan = QInputDialog::getInt(this, QStringLiteral("余额充值"), QStringLiteral("充值金额（元）"), 50, 1, 100000, 10, &ok);
    if (!ok) {
        return;
    }

    QJsonObject body;
    body.insert(QLatin1String("amountFen"), qlonglong(yuan) * 100);
    body.insert(QLatin1String("note"), QStringLiteral("余额充值"));
    m_api.post(QStringLiteral("/me/wallet/topups"), body,
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_session.updateBalance(data.toObject().value(QLatin1String("balanceAfterFen")).toInteger());
                   loadTransactions();
               },
               [this](const ApiError &error) {
                   QMessageBox::warning(this, QStringLiteral("充值失败"),
                                        error.message.isEmpty() ? error.code : error.message);
               });
}

void ProfileView::loadTransactions()
{
    m_api.get(QStringLiteral("/me/wallet/transactions"),
              [this](const QJsonValue &data, const QJsonObject &) {
                  m_transactionList->clear();
                  const QJsonArray transactions = data.toArray();
                  if (transactions.isEmpty()) {
                      m_transactionList->addItem(QStringLiteral("暂无流水记录"));
                      return;
                  }
                  for (const QJsonValue &value : transactions) {
                      const QJsonObject object = value.toObject();
                      QString type;
                      if (object.value(QLatin1String("type")).toString() == QLatin1String("top_up")) {
                          type = QStringLiteral("充值");
                      } else if (object.value(QLatin1String("type")).toString() == QLatin1String("charge_debit")) {
                          type = QStringLiteral("充电扣款");
                      } else if (object.value(QLatin1String("type")).toString() == QLatin1String("refund")) {
                          type = QStringLiteral("退款");
                      } else {
                          type = QStringLiteral("调整");
                      }
                      const qlonglong amount = object.value(QLatin1String("amountFen")).toInteger();
                      const QString item = QStringLiteral("%1  %2%3 元　余额 %4 元")
                                               .arg(type,
                                                    amount >= 0 ? QStringLiteral("+") : QStringLiteral("-"),
                                                    fenToYuan(qAbs(amount)))
                                               .arg(fenToYuan(object.value(QLatin1String("balanceAfterFen")).toInteger()));
                      m_transactionList->addItem(item);
                  }
              },
              [](const ApiError &) {});
}
