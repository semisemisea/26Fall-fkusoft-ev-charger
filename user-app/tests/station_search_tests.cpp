#include "api/api_client.h"
#include "app/session.h"
#include "views/station_list_view.h"
#include "widgets/station_card.h"

#include <QComboBox>
#include <QDialog>
#include <QJsonDocument>
#include <QTimer>
#ifdef USER_APP_HAS_WEBENGINE
#include <QWebEngineView>
#endif
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

// 控制每个响应的时机，覆盖真实 HTTP 请求及交错返回。
class StationSearchTests : public QObject {
	Q_OBJECT
	QTcpServer server;
	QList<QTcpSocket *> sockets;
	QList<QUrl> requests;

	void respond(int index, const QByteArray &body, int status = 200) {
		auto *socket = sockets.at(index);
		socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
		socket->disconnectFromHost();
	}

	static void click(StationListView &view, const char *name) {
		view.findChild<QPushButton *>(QString::fromLatin1(name))->click();
	}

private slots:
	void init() {
		requests.clear();
		sockets.clear();
		QVERIFY(server.listen(QHostAddress::LocalHost));
		connect(&server, &QTcpServer::newConnection, this, [this] {
			auto *socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
				const auto request = socket->property("request").toByteArray() + socket->readAll();
				socket->setProperty("request", request);
				if (!request.contains("\r\n\r\n") || socket->property("handled").toBool())
					return;
				socket->setProperty("handled", true);
				sockets.append(socket);
				requests.append(QUrl::fromEncoded(request.split(' ').at(1)));
			});
		});
	}

	void cleanup() {
		server.close();
		server.disconnect(this);
		qDeleteAll(server.findChildren<QTcpSocket *>());
	}

	void coordinatesRequireConfirmationAndIgnoreStaleResults() {
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		Session session;
		StationListView view(session, api);
		view.show();
		QVERIFY(view.findChildren<QComboBox *>().isEmpty());
		QVERIFY(!view.findChild<QLineEdit *>(QStringLiteral("addressEdit")));
		auto *latitude = view.findChild<QLineEdit *>(QStringLiteral("latitudeEdit"));
		auto *longitude = view.findChild<QLineEdit *>(QStringLiteral("longitudeEdit"));
		latitude->setText(QStringLiteral("38.881"));
		longitude->setText(QStringLiteral("121.584"));
		QTest::qWait(50);
		QCOMPARE(requests.size(), 0);
		QCOMPARE(session.latitude(), 38.914);
		click(view, "searchCoordinatesButton");
		QTRY_COMPARE(requests.size(), 1);
		QCOMPARE(requests[0].path(), QStringLiteral("/api/v1/stations/nearby"));
		QUrlQuery query(requests[0]);
		QCOMPARE(query.queryItemValue(QStringLiteral("latitude")), QStringLiteral("38.881000"));
		QCOMPARE(query.queryItemValue(QStringLiteral("longitude")), QStringLiteral("121.584000"));
		QCOMPARE(query.queryItemValue(QStringLiteral("sort")), QStringLiteral("distance"));
		respond(0, R"({"data":[{"id":1,"name":"近站","distanceKm":1},{"id":2,"name":"远站","distanceKm":4}]})");
		QTRY_COMPARE(view.findChildren<StationCard *>().size(), 2);
		QCOMPARE(view.findChildren<StationCard *>()[0]->station().id, 1LL);
		QCOMPARE(view.findChildren<StationCard *>()[1]->station().id, 2LL);

		click(view, "searchCoordinatesButton");
		QTRY_COMPARE(requests.size(), 2);
		latitude->setText(QStringLiteral("40"));
		longitude->setText(QStringLiteral("116"));
		click(view, "searchCoordinatesButton");
		QTRY_COMPARE(requests.size(), 3);
		respond(2, R"({"data":[{"id":3,"name":"新位置电站","distanceKm":2}]})");
		QTRY_COMPARE(view.findChildren<StationCard *>().size(), 1);
		respond(1, R"({"data":[{"id":99}]})");
		QTest::qWait(100);
		QCOMPARE(session.latitude(), 40.0);
		QCOMPARE(session.longitude(), 116.0);
		QCOMPARE(view.findChild<StationCard *>()->station().id, 3LL);

		view.hide();
		view.show();
		QTRY_COMPARE(requests.size(), 4);
		QCOMPARE(QUrlQuery(requests[3]).queryItemValue(QStringLiteral("latitude")), QStringLiteral("40.000000"));
		respond(3, R"({"data":[]})");
		QTRY_VERIFY(view.findChild<QLabel *>(QStringLiteral("error"))->isVisible());
		QVERIFY(view.findChildren<StationCard *>().isEmpty());
	}

	void validatesCoordinates() {
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		Session session;
		StationListView view(session, api);
		for (const QString &invalid : {QString(), QStringLiteral("nan"), QStringLiteral("91"), QStringLiteral("abc")}) {
			view.findChild<QLineEdit *>(QStringLiteral("latitudeEdit"))->setText(invalid);
			click(view, "searchCoordinatesButton");
			QCOMPARE(session.latitude(), 38.914);
			QVERIFY(view.findChild<QLabel *>(QStringLiteral("error"))->text().contains(QStringLiteral("有效坐标")));
		}
		view.findChild<QLineEdit *>(QStringLiteral("latitudeEdit"))->setText(QStringLiteral("38"));
		view.findChild<QLineEdit *>(QStringLiteral("longitudeEdit"))->setText(QStringLiteral("181"));
		click(view, "searchCoordinatesButton");
		QCOMPARE(session.latitude(), 38.914);
		QTest::qWait(50);
		QCOMPARE(requests.size(), 0);
	}

	void mapCancellationKeepsCoordinates() {
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		Session session;
		StationListView view(session, api);
		const auto savedKey = qgetenv("TENCENT_MAP_KEY");
		qunsetenv("TENCENT_MAP_KEY");
		bool opened = false;
		QTimer::singleShot(0, &view, [&] {
			auto *dialog = view.findChild<QDialog *>(QStringLiteral("mapPickerDialog"));
			if (dialog) {
				opened = true;
				dialog->reject();
			}
		});
		click(view, "pickLocationButton");
		qputenv("TENCENT_MAP_KEY", savedKey);
		QVERIFY(opened);
		QCOMPARE(view.findChild<QLineEdit *>(QStringLiteral("latitudeEdit"))->text(), QStringLiteral("38.914000"));
		QCOMPARE(requests.size(), 0);
	}

	void mapSelectionFillsCoordinatesBeforeConfirmation() {
#ifdef USER_APP_HAS_WEBENGINE
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		Session session;
		StationListView view(session, api);
		const auto savedKey = qgetenv("TENCENT_MAP_KEY");
		qputenv("TENCENT_MAP_KEY", "test-key");
		bool selected = false;
		QTimer::singleShot(0, &view, [&] {
			auto *dialog = view.findChild<QDialog *>(QStringLiteral("mapPickerDialog"));
			if (!dialog)
				return;
			auto *map = dialog->findChild<QWebEngineView *>();
			if (map) {
				// 管理端测试覆盖消息来源校验，此处验证同一选点回调与用户端表单的连接。
				selected = QMetaObject::invokeMethod(map, "titleChanged", Qt::DirectConnection,
													 Q_ARG(QString, QStringLiteral("ev-charger-location:38.88202,121.5879")));
			}
			if (dialog->result() != QDialog::Accepted)
				dialog->reject();
		});
		click(view, "pickLocationButton");
		qputenv("TENCENT_MAP_KEY", savedKey);
		QVERIFY(selected);
		QCOMPARE(view.findChild<QLineEdit *>(QStringLiteral("latitudeEdit"))->text(), QStringLiteral("38.882020"));
		QCOMPARE(view.findChild<QLineEdit *>(QStringLiteral("longitudeEdit"))->text(), QStringLiteral("121.587900"));
		QCOMPARE(session.latitude(), 38.914);
		QCOMPARE(requests.size(), 0);
		click(view, "searchCoordinatesButton");
		QTRY_COMPARE(requests.size(), 1);
		QCOMPARE(QUrlQuery(requests[0]).queryItemValue(QStringLiteral("latitude")), QStringLiteral("38.882020"));
		respond(0, R"({"data":[]})");
		QTRY_VERIFY(view.findChild<QLabel *>(QStringLiteral("error"))->text().contains(QStringLiteral("暂无")));
#else
		QSKIP("Qt WebEngine is unavailable");
#endif
	}
};

QTEST_MAIN(StationSearchTests)
#include "station_search_tests.moc"
