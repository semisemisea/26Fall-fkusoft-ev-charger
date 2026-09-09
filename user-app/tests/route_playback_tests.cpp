#include "models/route_playback.h"
#include <QJsonArray>
#include <QTest>

class RoutePlaybackTests : public QObject {
	Q_OBJECT
private slots:
	void followsDistanceAndStopsAtDestination() {
		RoutePlayback playback;
		const QJsonObject route{{"distanceM", 3000}, {"durationSec", 300}, {"polyline", QJsonArray{QJsonArray{0, 0}, QJsonArray{0, .01}, QJsonArray{0, .03}}}, {"steps", QJsonArray{QJsonObject{{"instruction", "first"}, {"distanceM", 1000}}, QJsonObject{{"instruction", "second"}, {"distanceM", 2000}}}}};
		QVERIFY(playback.load(route));
		QCOMPARE(playback.position(), QPointF(0, 0));
		QCOMPARE(playback.stepIndex(), 0);
		playback.advance(150);
		QVERIFY(qAbs(playback.position().y() - .015) < 1e-8);
		QCOMPARE(playback.remainingMeters(), 1500);
		QCOMPARE(playback.stepIndex(), 1);
		playback.advance(9999);
		QVERIFY(playback.finished());
		QCOMPARE(playback.position(), QPointF(0, .03));
		QCOMPARE(playback.remainingMeters(), 0);
		playback.reset();
		QVERIFY(!playback.finished());
		QCOMPARE(playback.position(), QPointF(0, 0));
	}
	void rejectsBadRoutesAndClearsPreviousState() {
		RoutePlayback playback;
		QJsonObject route{{"distanceM", 100}, {"durationSec", 10}, {"polyline", QJsonArray{QJsonArray{38, 121}, QJsonArray{38, 121}, QJsonArray{39, 121}}}};
		QVERIFY(playback.load(route));
		QCOMPARE(playback.position(), QPointF(38, 121));
		playback.advance(5);
		QVERIFY(qIsFinite(playback.position().x()));
		QCOMPARE(playback.stepIndex(), -1);
		route["polyline"] = QJsonArray{QJsonArray{91, 121}, QJsonArray{38, 121}};
		QVERIFY(!playback.load(route));
		QVERIFY(!playback.ready());
		route["polyline"] = QJsonArray{QJsonArray{38, 121}, QJsonArray{38, 121}};
		QVERIFY(!playback.load(route));
	}
};
QTEST_GUILESS_MAIN(RoutePlaybackTests)
#include "route_playback_tests.moc"
