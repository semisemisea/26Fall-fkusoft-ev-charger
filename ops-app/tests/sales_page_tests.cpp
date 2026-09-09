#include "api/api_client.h"
#include "pages/sales_page.h"

#include <QLabel>
#include <QTest>

class SalesPageTests : public QObject {
	Q_OBJECT
private slots:
	void revenueTitlesSurviveUpdates() {
		ops::ApiClient client;
		SalesPage page(&client);
		QList<QLabel *> cards;
		for (auto *label : page.findChildren<QLabel *>()) {
			if (label->property("card").toBool())
				cards.append(label);
		}
		QCOMPARE(cards.size(), 3);
		const QStringList titles{QStringLiteral("今日营收 (元)"), QStringLiteral("本月营收 (元)"), QStringLiteral("累计营收 (元)")};
		for (int i = 0; i < cards.size(); ++i)
			QVERIFY(cards[i]->text().contains(titles[i]));
		ops::DashboardSummary summary;
		summary.todayRevenueFen = 12345;
		summary.monthRevenueFen = 67890;
		summary.totalRevenueFen = 123456;
		emit client.dashboardSummaryFetched(summary, {});
		const QStringList values{QStringLiteral("123.45"), QStringLiteral("678.90"), QStringLiteral("1234.56")};
		for (int i = 0; i < cards.size(); ++i) {
			QVERIFY(cards[i]->text().contains(titles[i]));
			QVERIFY(cards[i]->text().contains(values[i]));
		}
		emit client.dashboardSummaryFetched({}, {});
		emit client.dashboardSummaryFetched({}, QStringLiteral("NETWORK_ERROR"));
		for (int i = 0; i < cards.size(); ++i) {
			QVERIFY(cards[i]->text().contains(titles[i]));
			QVERIFY(cards[i]->text().contains(QStringLiteral("0.00")));
		}
	}
};

QTEST_MAIN(SalesPageTests)
#include "sales_page_tests.moc"
