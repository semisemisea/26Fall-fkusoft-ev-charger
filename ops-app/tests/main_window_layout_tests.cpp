/** @file
 * @brief 管理员前端回归测试；用本地响应或直接发送信号隔离真实服务。
 */
#include "api/api_client.h"
#include "main-window/main_window.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTest>

/// @brief 主窗口导航布局回归测试。
class MainWindowLayoutTests : public QObject {
	Q_OBJECT

private slots:
	/// @brief 验证主窗口使用水平布局且侧栏位于页面堆栈左侧。
	void sidebarIsLeftOfPageStack();
};

/// @brief 验证主窗口使用水平布局且侧栏位于页面堆栈左侧。

void MainWindowLayoutTests::sidebarIsLeftOfPageStack() {
	ops::ApiClient client;
	MainWindow window(&client);
	auto *layout = qobject_cast<QHBoxLayout *>(window.centralWidget()->layout());
	auto *sidebar = window.findChild<QWidget *>(QStringLiteral("sidebar"));
	auto *stack = window.findChild<QStackedWidget *>();
	QVERIFY(layout);
	QVERIFY(sidebar);
	QVERIFY(stack);
	QCOMPARE(layout->indexOf(sidebar), 0);
	QCOMPARE(layout->indexOf(stack), 1);
}

QTEST_MAIN(MainWindowLayoutTests)

#include "main_window_layout_tests.moc"
