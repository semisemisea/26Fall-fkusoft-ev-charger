#pragma once

// 管理后台主窗口:左侧导航 + 右侧 QStackedWidget 页面栈。
// 布局参考 Qt 官方 Thermostat 示例的深色卡片视觉,用 Widgets + QSS 实现。

#include <QMainWindow>

#include "api/apiclient.h"

class QLabel;
class QListWidget;
class QStackedWidget;
class SalesPage;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(ops::ApiClient *api, QWidget *parent = nullptr);
	~MainWindow();

	// 401 失去认证后由 main 驱动重新登录,成功后调用此方法恢复界面
	void resumeAfterLogin();

private:
	void buildSidebar();
	QWidget *createPage(int index);

	ops::ApiClient *m_api; // 非所有权引用,生命周期由 main 管理

	QListWidget *m_navList = nullptr;
	QStackedWidget *m_stack = nullptr;
	QLabel *m_userLabel = nullptr;
};
