#include "../page_refresh.h"
#include <QDialog>
#include <QTest>

class RefreshTests : public QObject {
	Q_OBJECT
private slots:
	void configuration() {
		QSettings settings(QCoreApplication::applicationDirPath() + QStringLiteral("/refresh.ini"), QSettings::IniFormat);
		for (const auto &value : {QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("bad"), QStringLiteral("2147484")}) {
			settings.setValue(QStringLiteral("refresh/intervalSeconds"), value);
			settings.sync();
			QCOMPARE(evcharger::refreshIntervalMs(), 3000);
		}
		settings.setValue(QStringLiteral("refresh/intervalSeconds"), 1);
		settings.sync();
		QCOMPARE(evcharger::refreshIntervalMs(), 1000);
	}
	void visibilityAndModalPause() {
		QWidget page;
		int calls = 0;
		new evcharger::PageRefresh(&page, [&] { ++calls; });
		page.show();
		QCOMPARE(calls, 1);
		QTRY_VERIFY_WITH_TIMEOUT(calls >= 2, 1600);
		page.hide();
		const int hiddenCalls = calls;
		QTest::qWait(1200);
		QCOMPARE(calls, hiddenCalls);
		page.show();
		QCOMPARE(calls, hiddenCalls + 1);
		QDialog dialog(&page);
		dialog.setModal(true);
		dialog.show();
		const int modalCalls = calls;
		QTest::qWait(1200);
		QCOMPARE(calls, modalCalls);
		dialog.close();
		QTRY_VERIFY_WITH_TIMEOUT(calls > modalCalls, 1600);
	}
	void cleanupTestCase() {
		QFile::remove(QCoreApplication::applicationDirPath() + QStringLiteral("/refresh.ini"));
	}
};
QTEST_MAIN(RefreshTests)
#include "refresh_tests.moc"
