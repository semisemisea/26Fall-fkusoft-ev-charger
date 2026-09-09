#include "views/navigation_view.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QWheelEvent>
#ifdef USER_APP_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

class NavigationTests : public QObject {
	Q_OBJECT
private slots:

#ifdef USER_APP_HAS_WEBENGINE
	void failedMapKeepsFallbackInteractive() {
		QWebEngineView view;
		view.resize(600, 400);
		view.show();
		QSignalSpy loaded(&view, &QWebEngineView::loadFinished);
		view.load(QUrl("qrc:/navigation/map.html"));
		QVERIFY(loaded.wait(5000));
		auto evaluate = [&](const QString &script) {
			QEventLoop loop;
			QVariant result;
			const QPointer<QEventLoop> guard(&loop);
			view.page()->runJavaScript(script, [&, guard](const QVariant &value) {
				if (!guard)
					return;
				result = value;
				loop.quit();
			});
			QTimer::singleShot(5000, &loop, &QEventLoop::quit);
			loop.exec();
			return result;
		};
		// 模拟 SDK 创建覆盖层后因 WebGL 不可用抛出异常。
		evaluate(R"JS(
showRoute({key:'',route:{polyline:[[38,121],[38.01,121.01]]}});
window.TMap={LatLng:function(){},Map:function(){
 const layer=document.getElementById('map');
 layer.style.zIndex='500';layer.style.pointerEvents='auto';
 layer.innerHTML='<div style="position:absolute;inset:0"></div>';
 throw new Error('WebGL unavailable');
}};
initializeMap();
)JS");
		QCOMPARE(evaluate("document.elementFromPoint(200,200).id").toString(), QStringLiteral("fallback"));
		auto *target = view.focusProxy();
		QVERIFY(target);
		const QPoint point(200, 200);
		QWheelEvent wheel(QPointF(point), QPointF(target->mapToGlobal(point)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(target, &wheel);
		QTRY_VERIFY(evaluate("scale > 1").toBool());
		QTest::mousePress(target, Qt::LeftButton, Qt::NoModifier, point);
		QMouseEvent move(QEvent::MouseMove, QPointF(point + QPoint(40, 20)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(target, &move);
		QTest::mouseRelease(target, Qt::LeftButton, Qt::NoModifier, point + QPoint(40, 20));
		QTRY_VERIFY(evaluate("panX !== 0 || panY !== 0").toBool());
		evaluate("document.getElementById('fit').click()");
		QVERIFY(evaluate("scale === 1 && panX === 0 && panY === 0").toBool());
	}
#endif
	void plansOnEntryAndSimulatesWithoutExternalNavigation() {
		qunsetenv("TENCENT_MAP_KEY"); // 本测试只访问本地 HTTP 与 qrc 页面。
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QList<QUrl> requests;
		connect(&server, &QTcpServer::newConnection, this, [&] {
			auto *socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				auto buffer = socket->property("buffer").toByteArray() + socket->readAll();
				socket->setProperty("buffer", buffer);
				if (!buffer.contains("\r\n\r\n") || socket->property("handled").toBool())
					return;
				socket->setProperty("handled", true);
				requests.append(QUrl::fromEncoded(buffer.split(' ')[1]));
				QJsonObject route{{"distanceM", 1000}, {"durationSec", 600}, {"polyline", QJsonArray{QJsonArray{38, 121}, QJsonArray{38.01, 121.01}}}, {"steps", QJsonArray{QJsonObject{{"instruction", "沿测试道路直行"}, {"distanceM", 1000}}}}};
				const auto body = QJsonDocument(QJsonObject{{"data", route}}).toJson();
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		});
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		Session session;
		NavigationView view(session, api);
		view.resize(900, 700);
		view.show();
		Station station;
		station.name = QStringLiteral("测试充电站");
		station.latitude = 38.01;
		station.longitude = 121.01;
		view.open(station);
		auto *play = view.findChild<QPushButton *>("playSimulation");
		QVERIFY(!play->isEnabled());
		QTRY_COMPARE(requests.size(), 1);
		QCOMPARE(QUrlQuery(requests[0]).queryItemValue("mode"), QStringLiteral("driving"));
		QTRY_VERIFY(play->isEnabled());
		auto *steps = view.findChild<QListWidget *>("routeSteps");
		QCOMPARE(steps->item(0)->text(), QStringLiteral("沿测试道路直行"));
#ifdef USER_APP_HAS_WEBENGINE
		auto *map = view.findChild<QWebEngineView *>("routeMap");
		QVERIFY(map);
		const auto text = QSharedPointer<QString>::create();
		QTRY_VERIFY_WITH_TIMEOUT(([&] {
									 map->page()->runJavaScript("document.getElementById('notice') ? document.getElementById('notice').textContent : ''", [text](const QVariant &value) { *text = value.toString(); });
									 return text->contains(QStringLiteral("底图未配置"));
								 })(),
								 10000);
		QCOMPARE(map->url(), QUrl("qrc:/navigation/map.html"));
		if (qEnvironmentVariableIsSet("NAVIGATION_TEST_SCREENSHOT"))
			view.grab().save(qEnvironmentVariable("NAVIGATION_TEST_SCREENSHOT"));
#endif
		auto *progress = view.findChild<QLabel *>("simulationProgress");
		const auto initialProgress = progress->text();
		play->click();
		QCOMPARE(play->text(), QStringLiteral("暂停"));
		QTRY_VERIFY(progress->text() != initialProgress);
		view.hide();
		QCOMPARE(play->text(), QStringLiteral("开始模拟"));
		const auto pausedProgress = progress->text();
		QTest::qWait(200);
		QCOMPARE(progress->text(), pausedProgress);
		view.show();
		view.findChild<QPushButton *>("restartSimulation")->click();
		QCOMPARE(play->text(), QStringLiteral("开始模拟"));
		QCOMPARE(progress->text(), initialProgress);
		view.findChild<QPushButton *>("walking")->click();
		QTRY_COMPARE(requests.size(), 2);
		QCOMPARE(QUrlQuery(requests[1]).queryItemValue("mode"), QStringLiteral("walking"));
		QTRY_VERIFY(play->isEnabled());
	}
};
QTEST_MAIN(NavigationTests)
#include "navigation_tests.moc"
