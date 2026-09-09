#include "app/application_controller.h"
#include "api/api_client.h"
#include "login-dialog/login_dialog.h"
#include "main-window/main_window.h"

#include <QApplication>

ApplicationController::ApplicationController(ops::ApiClient &api, QObject *parent)
	: QObject(parent), m_api(api) {
	// 排队切换，避免在网络响应或控件信号的调用栈中销毁窗口。
	connect(&m_api, &ops::ApiClient::authenticationChanged, this, [this](bool authenticated) {
        if (!authenticated && m_window && m_window->isVisible())
            showLogin(); }, Qt::QueuedConnection);
}

ApplicationController::~ApplicationController() = default;

void ApplicationController::start() {
	if (!m_login && !m_window)
		showLogin();
}

void ApplicationController::showLogin() {
	if (m_login)
		return;
	m_login = std::make_unique<LoginDialog>(&m_api);
	// 登录和窗口切换期间可能没有可见主窗口；取消登录由 rejected 明确退出。
	qApp->setQuitOnLastWindowClosed(false);
	connect(m_login.get(), &QDialog::accepted, this,
			&ApplicationController::showMainWindow, Qt::QueuedConnection);
	connect(m_login.get(), &QDialog::rejected, qApp, &QCoreApplication::quit);
	m_login->open();
	// 释放旧会话的页面、定时器和子对话框，重新登录后重新构建。
	m_window.reset();
}

void ApplicationController::showMainWindow() {
	m_window = std::make_unique<MainWindow>(&m_api);
	m_window->show();
	m_login.reset();
	qApp->setQuitOnLastWindowClosed(true);
}
