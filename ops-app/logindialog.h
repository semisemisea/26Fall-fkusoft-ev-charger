#pragma once

// 管理员登录对话框。登录成功后把令牌保存在共享的 ApiClient 中,
// 对话框自身不持有业务数据,通过信号把结果交给 main。

#include <QDialog>

#include "api/apiclient.h"

class QLineEdit;
class QPushButton;

namespace Ui {
	class LoginDialog;
}

class LoginDialog : public QDialog {
	Q_OBJECT
public:
	explicit LoginDialog(ops::ApiClient *api, QWidget *parent = nullptr);
	~LoginDialog();

signals:
	void loginSucceeded();

protected:
	void accept() override; // 拦截默认行为:改为走异步登录

private:
	Ui::LoginDialog *ui;
	ops::ApiClient *m_api; // 非所有权引用,生命周期由 main 管理
};
