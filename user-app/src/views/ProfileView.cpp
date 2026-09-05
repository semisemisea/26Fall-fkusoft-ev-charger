#include "ProfileView.h"

#include "common/Format.h"
#include "models/User.h"
#include "widgets/Toast.h"
#include "widgets/RechargeDialog.h"
#include "widgets/ScaleButton.h"
#include "common/Theme.h"
#include "widgets/AppIcons.h"

#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHttpMultiPart>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const QLatin1String kDefaultAvatarStyle{
    "QLabel { background: rgba(255,255,255,0.35); border-radius: 36px; color: white; font-size: 36px; }"};
const QLatin1String kAvatarPixmapStyle{"QLabel { border-radius: 36px; }"};

QString maskedPhone(const QString &phone)
{
    return phone.length() == 11
               ? QStringLiteral("%1****%2").arg(phone.left(3), phone.right(4))
               : phone;
}
}

ProfileView::ProfileView(Session &session, ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_api(api)
{
    auto *headerCard = new QFrame(this);
    headerCard->setObjectName(QStringLiteral("profileHeader"));

    m_avatarLabel = new QLabel(QStringLiteral("👤"), headerCard);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    m_avatarLabel->setFixedSize(72, 72);
    m_avatarLabel->setStyleSheet(kDefaultAvatarStyle);
    m_avatarLabel->setCursor(Qt::PointingHandCursor);
    m_avatarLabel->installEventFilter(this);
    m_avatarLabel->setToolTip(QStringLiteral("点击更换头像"));

    m_nicknameLabel = new QLabel(headerCard);
    m_nicknameLabel->setObjectName(QStringLiteral("profileName"));

    auto *editButton = new QPushButton(QStringLiteral("✏️"), headerCard);
    editButton->setObjectName(QStringLiteral("iconGhost"));
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setToolTip(QStringLiteral("修改昵称"));
    editButton->setFixedSize(24, 24);

    m_phoneLabel = new QLabel(headerCard);
    m_phoneLabel->setObjectName(QStringLiteral("profilePhone"));

    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->setSpacing(4);
    nicknameRow->addWidget(m_nicknameLabel);
    nicknameRow->addWidget(editButton);
    nicknameRow->addStretch();

    auto *infoColumn = new QVBoxLayout;
    infoColumn->addStretch();
    infoColumn->addLayout(nicknameRow);
    infoColumn->addWidget(m_phoneLabel);
    infoColumn->addStretch();

    auto *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(18, 18, 18, 18);
    headerLayout->setSpacing(14);
    headerLayout->addWidget(m_avatarLabel);
    headerLayout->addLayout(infoColumn, 1);

    auto *walletCard = new QFrame(this);
    walletCard->setObjectName(QStringLiteral("profileCard"));
    auto *balanceTitle = new QLabel(QStringLiteral("钱包余额"), walletCard);
    balanceTitle->setObjectName(QStringLiteral("muted"));
    balanceTitle->setAlignment(Qt::AlignCenter);
    m_balanceLabel = new QLabel(walletCard);
    m_balanceLabel->setAlignment(Qt::AlignCenter);
    m_balanceLabel->setObjectName(QStringLiteral("balance"));
    auto *topUpButton = new ScaleButton(QStringLiteral("立即充值"), walletCard);
    topUpButton->setObjectName(QStringLiteral("primaryButton"));

    auto *walletLayout = new QVBoxLayout(walletCard);
    walletLayout->setContentsMargins(16, 16, 16, 16);
    walletLayout->setSpacing(8);
    walletLayout->addWidget(balanceTitle);
    walletLayout->addWidget(m_balanceLabel);
    walletLayout->addSpacing(4);
    walletLayout->addWidget(topUpButton);

    const struct
    {
        const char *text;
        void (ProfileView::*signal)();
    } menuItems[] = {
        {"📋  历史充电订单", &ProfileView::ordersRequested},
        {"📅  我的预约记录", &ProfileView::reservationsRequested},
        {"💰  钱包流水", &ProfileView::transactionsRequested},
        {"🚗  我的爱车", &ProfileView::carRequested},
        {"ℹ️  关于系统", &ProfileView::aboutRequested},
    };

    auto *menuCard = new QFrame(this);
    menuCard->setObjectName(QStringLiteral("menuCard"));
    auto *menuLayout = new QVBoxLayout(menuCard);
    menuLayout->setContentsMargins(4, 4, 4, 4);
    menuLayout->setSpacing(0);

	for (const auto &item : menuItems) {
		auto *row = new QPushButton(menuCard);
		// 只对 "历史充电订单" 加 clock 图标
		QString text = QString::fromUtf8(item.text) + QStringLiteral("   ›");
		if (QString::fromUtf8(item.text) == QStringLiteral("📋  历史充电订单")) {
			QIcon icon(AppIcons::clock(theme::primary(), 20, false));
			row->setIcon(icon);
			row->setIconSize(QSize(20, 20));
			row->setText(QStringLiteral("  历史充电订单   ›"));  // 去掉 📋，用图标替代
		} else {
			row->setText(text);
		}
		connect(row, &QPushButton::clicked, this, item.signal);
		menuLayout->addWidget(row);
	}

    auto *logoutButton = new ScaleButton(QStringLiteral("退出登录"), this);
    logoutButton->setObjectName(QStringLiteral("outlineDangerButton"));
    connect(logoutButton, &QPushButton::clicked, this, &ProfileView::signOut);
    connect(editButton, &QPushButton::clicked, this, &ProfileView::changeNickname);
    connect(topUpButton, &QPushButton::clicked, this, &ProfileView::openRecharge);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);
    layout->addWidget(headerCard);
    layout->addWidget(walletCard);
    layout->addWidget(menuCard);
    layout->addStretch();
    layout->addWidget(logoutButton);

    connect(&m_session, &Session::userChanged, this, &ProfileView::refreshProfile);
}

void ProfileView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshProfile();
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
    m_phoneLabel->setText(maskedPhone(user.phone));
    m_balanceLabel->setText(QStringLiteral("￥ %1").arg(fenToYuan(user.walletBalanceFen)));
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
        Toast::error(this, QStringLiteral("无法读取所选文件"));
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
                     Toast::error(this, error.message.isEmpty() ? error.code : error.message);
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
                    Toast::error(this, error.message.isEmpty() ? error.code : error.message);
                });
}

void ProfileView::openRecharge()
{
    auto *dialog = new RechargeDialog(m_api, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &RechargeDialog::succeeded, this, [this](qlonglong balance) {
        m_session.updateBalance(balance);
    });
    dialog->open();
}

void ProfileView::signOut()
{
    const auto choice = QMessageBox::question(this, QStringLiteral("退出登录"),
                                              QStringLiteral("确定退出当前账号吗？"));
    if (choice == QMessageBox::Yes) {
        m_session.signOut();
    }
}
