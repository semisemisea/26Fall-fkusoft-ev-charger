#include "logindialog.h"
#include "ui_logindialog.h"

#include <QPushButton>

LoginDialog::LoginDialog(ops::ApiClient *api, QWidget *parent)
	: QDialog(parent), ui(new Ui::LoginDialog), m_api(api) {
	ui->setupUi(this);
	ui->passwordEdit->setEchoMode(QLineEdit::EchoMode::Password);
	setWindowTitle(tr("充电桩管理平台 - 管理员登录"));

	connect(ui->loginButton, &QPushButton::clicked, this, &LoginDialog::accept);
	connect(m_api, &ops::ApiClient::loginSucceeded, this, [this](const ops::AdminUser &admin) {
		ui->messageLabel->setText(
			tr("欢迎, %1 (%2)").arg(admin.displayName, admin.role));
		emit loginSucceeded();
	});
	connect(m_api, &ops::ApiClient::loginFailed, this,
			[this](const QString &code, const QString &message) {
				ui->messageLabel->setText(
					tr("登录失败: %1 (%2)").arg(message, code));
				ui->loginButton->setEnabled(true);
			});
}

LoginDialog::~LoginDialog() { delete ui; }

void LoginDialog::accept() {
	const QString username = ui->usernameEdit->text().trimmed();
	const QString password = ui->passwordEdit->text();
	if (username.isEmpty() || password.isEmpty()) {
		ui->messageLabel->setText(tr("请输入账号和密码"));
		return;
	}
	ui->loginButton->setEnabled(false);
	ui->messageLabel->setText(tr("登录中..."));
	m_api->login(username, password);
}
