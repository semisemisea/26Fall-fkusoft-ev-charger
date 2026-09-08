/** @file
 * @brief 管理员登录表单，协调输入校验、异步认证结果和模态对话框接受状态。
 */
#pragma once

// 管理员登录对话框。登录成功后把令牌保存在共享的 ApiClient 中,
// 对话框自身不持有业务数据,通过信号把结果交给应用控制器。

#include <QDialog>

#include "api/api_client.h"

class QLineEdit;
class QPushButton;

namespace Ui {
	class LoginDialog;
}

/// @brief 将登录按钮转换为异步认证的模态对话框；成功后才接受对话框。
class LoginDialog : public QDialog {
	Q_OBJECT
public:
	/** @brief 安装登录表单和品牌图标，将异步成功、失败信号连接到对话框状态。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit LoginDialog(ops::ApiClient *api, QWidget *parent = nullptr);
	/** @brief 释放自身辅助资源；Qt 自动释放所属子对象。
	 */
	~LoginDialog();

signals:
	/** @brief 认证成功通知。
	 */
	void loginSucceeded();

protected:
	/** @brief 校验非空凭据并禁用登录按钮，等待异步认证结果后再关闭。
	 */
	void accept() override; // 拦截默认行为:改为走异步登录

private:
	Ui::LoginDialog *ui;   ///< 拥有的 uic 辅助对象；析构时显式释放，控件由对话框拥有。
	ops::ApiClient *m_api; ///< 非拥有的共享客户端，须比界面对象存活更久。
};
