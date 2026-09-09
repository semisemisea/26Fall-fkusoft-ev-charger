/**
 * @file login_view.cpp
 * @brief 校验手机号并通过登录接口建立用户会话。
 */
#include <evcharger/logging.h>

#include "login_view.h"

#include "models/user.h"
#include "widgets/scale_button.h"
#include "widgets/toast.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(userLoginViewLog, "evcharger.user.ui", QtInfoMsg)

namespace {
	/// @brief 登录输入校验：仅接受恰好十一位数字。
	const QRegularExpression kPhonePattern{QLatin1String("\\d{11}")};
} // namespace

/**
 * @details 构造函数：搭建登录界面（Logo/标题/手机号输入/登录按钮），回车或点击均触发 submit
 */
LoginView::LoginView(Session &session, ApiClient &api, QWidget *parent)
	: QWidget(parent), m_session(session), m_api(api) {
	if (objectName().isEmpty())
		setObjectName(QStringLiteral("LoginView"));
	EV_LOG_DEBUG(userLoginViewLog, this) << "View initialized";
	auto *logoLabel = new QLabel(this);
	logoLabel->setAlignment(Qt::AlignCenter);
	logoLabel->setObjectName(QStringLiteral("prepareIcon"));
	logoLabel->setPixmap(QPixmap(QStringLiteral(":/backgrounds/logo.png"))
							 .scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	logoLabel->setFixedSize(160, 160);

	auto *title = new QLabel(QStringLiteral("欢迎使用智能充电系统"), this);
	title->setAlignment(Qt::AlignCenter);
	title->setObjectName(QStringLiteral("pageHeading"));

	auto *hint = new QLabel(QStringLiteral("输入 11 位手机号，免密登录\n新手机号将自动注册"), this);
	hint->setAlignment(Qt::AlignCenter);

	m_phoneEdit = new QLineEdit(this);
	m_phoneEdit->setObjectName(QStringLiteral("loginPhoneInput"));
	m_phoneEdit->setFixedWidth(280);
	m_phoneEdit->setMinimumHeight(55);
	m_phoneEdit->setPlaceholderText(QStringLiteral("请输入11位手机号码"));
	m_phoneEdit->setValidator(new QRegularExpressionValidator(kPhonePattern, this));
	m_phoneEdit->setMaxLength(11);
	m_phoneEdit->setClearButtonEnabled(true);

	m_loginButton = new ScaleButton(QStringLiteral("登录 / 注册"), this);
	m_loginButton->setObjectName(QStringLiteral("loginSubmitButton"));
	m_loginButton->setFixedWidth(280);
	m_loginButton->setMinimumHeight(44);

	m_messageLabel = new QLabel(this);
	m_messageLabel->setAlignment(Qt::AlignCenter);
	m_messageLabel->setObjectName(QStringLiteral("error"));
	m_messageLabel->hide();

	// 输入框居中容器
	auto *phoneContainer = new QHBoxLayout();
	phoneContainer->addStretch();
	phoneContainer->addWidget(m_phoneEdit);
	phoneContainer->addStretch();

	// 按钮居中容器
	auto *btnContainer = new QHBoxLayout();
	btnContainer->addStretch();
	btnContainer->addWidget(m_loginButton);
	btnContainer->addStretch();

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(28, 12, 28, 12);

	layout->addStretch(1);
	layout->addWidget(logoLabel, 0, Qt::AlignHCenter);
	layout->addWidget(title);
	layout->addSpacing(8);
	layout->addWidget(hint);
	layout->addSpacing(16);
	layout->addLayout(phoneContainer);
	layout->addSpacing(18);
	layout->addLayout(btnContainer);
	layout->addWidget(m_messageLabel);
	layout->addStretch(4);

	connect(m_loginButton, &QPushButton::clicked, this, &LoginView::submit);
	connect(m_phoneEdit, &QLineEdit::returnPressed, this, &LoginView::submit);
}

/**
 * @details 提交登录：本地校验 11 位手机号，成功后写入 Session 并发 loginSucceeded；新用户提示自动注册
 */
void LoginView::submit() {
	EV_LOG_INFO(userLoginViewLog, this) << "Login submitted";
	const QString phone = m_phoneEdit->text().trimmed();
	if (!kPhonePattern.match(phone).hasMatch()) {
		EV_LOG_WARNING(userLoginViewLog, this) << "Login rejected: invalid phone format";
		m_messageLabel->setText(QStringLiteral("请输入 11 位数字手机号"));
		m_messageLabel->show();
		return;
	}

	m_loginButton->setEnabled(false);
	m_messageLabel->hide();

	QJsonObject body;
	body.insert(QLatin1String("phone"), phone);
	m_api.post(QStringLiteral("/auth/user/login"), body, [this](const QJsonValue &data, const QJsonObject &) {
				   m_loginButton->setEnabled(true);
				   const QJsonObject object = data.toObject();
				   const User user = User::fromJson(object.value(QLatin1String("user")).toObject());
				   if (object.value(QLatin1String("isNewUser")).toBool()) {
					   Toast::success(this, QStringLiteral("已自动注册，默认昵称：%1").arg(user.nickname));
				   }
				   m_session.signIn(user, object.value(QLatin1String("accessToken")).toString());
				   emit loginSucceeded(); }, [this](const ApiError &error) {
 EV_LOG_WARNING(userLoginViewLog, this) << "API operation failed in view";
				   m_loginButton->setEnabled(true);
				   m_messageLabel->setText(error.message.isEmpty() ? error.code : error.message);
				   m_messageLabel->show(); });
}
