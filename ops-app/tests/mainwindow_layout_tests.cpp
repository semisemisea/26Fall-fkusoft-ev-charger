#include "api/apiclient.h"
#include "mainwindow.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTest>

class MainWindowLayoutTests : public QObject {
	Q_OBJECT

private slots:
	void sidebarIsLeftOfPageStack();
};

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

#include "mainwindow_layout_tests.moc"
