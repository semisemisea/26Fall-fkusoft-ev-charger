#pragma once

#include <QObject>
#include <memory>

namespace ops {
	class ApiClient;
}
class LoginDialog;
class MainWindow;

/// 管理登录与主窗口的切换；客户端由调用者提供，必须比控制器存活更久。
/// 顶层窗口由 unique_ptr 拥有，窗口内部控件由 Qt 父子对象树管理。
class ApplicationController : public QObject {
	Q_OBJECT
public:
	explicit ApplicationController(ops::ApiClient &api, QObject *parent = nullptr);
	~ApplicationController() override;
	void start();

private:
	void showLogin();
	void showMainWindow();

	ops::ApiClient &m_api;
	std::unique_ptr<LoginDialog> m_login;
	std::unique_ptr<MainWindow> m_window;
};
