#include "api/apiclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class ChargerApiTests : public QObject {
	Q_OBJECT

private slots:
	void createsUpdatesAndDeletesCharger();
	void fetchesCompleteStatusOverview();
};

void ChargerApiTests::createsUpdatesAndDeletesCharger() {
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QList<QByteArray> requests;
	connect(&server, &QTcpServer::newConnection, this, [&] {
		QTcpSocket *socket = server.nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests] {
			QByteArray request = socket->property("requestBuffer").toByteArray();
			request.append(socket->readAll());
			socket->setProperty("requestBuffer", request);
			const qsizetype headerEnd = request.indexOf("\r\n\r\n");
			if (headerEnd < 0)
				return;
			qint64 contentLength = 0;
			for (const QByteArray &line : request.left(headerEnd).split('\n')) {
				if (line.toLower().startsWith("content-length:"))
					contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
			}
			if (request.size() < headerEnd + 4 + contentLength)
				return;
			requests.append(request);
			const QByteArray path = request.split('\n').first();
			QByteArray responseBody;
			QByteArray status = "200 OK";
			if (path.startsWith("POST ")) {
				status = "201 Created";
				responseBody = R"({"data":{"id":9},"meta":{"requestId":"test"}})";
			} else if (path.startsWith("PATCH ")) {
				responseBody = R"({"data":{"id":9},"meta":{"requestId":"test"}})";
			} else {
				status = "204 No Content";
			}
			const QByteArray response = "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
										QByteArray::number(responseBody.size()) + "\r\nConnection: close\r\n\r\n" + responseBody;
			socket->write(response);
			socket->disconnectFromHost();
		});
	});

	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
	QSignalSpy finished(&client, &ops::ApiClient::chargerMutationFinished);

	ops::ChargerForm form;
	form.type = QStringLiteral("fast");
	form.powerKw = 120.5;
	client.createCharger(4, form);
	QTRY_COMPARE(finished.count(), 1);
	QCOMPARE(requests.size(), 1);
	QVERIFY(requests.at(0).startsWith("POST /api/v1/admin/stations/4/chargers "));
	const QJsonObject createBody = QJsonDocument::fromJson(requests.at(0).split('\n').last()).object();
	QCOMPARE(createBody.value(QStringLiteral("type")).toString(), QStringLiteral("fast"));
	QCOMPARE(createBody.value(QStringLiteral("powerKw")).toDouble(), 120.5);
	QVERIFY(!createBody.contains(QStringLiteral("operationalStatus")));

	form.type = QStringLiteral("slow");
	form.powerKw = 7.25;
	form.operationalStatus = QStringLiteral("offline");
	client.updateCharger(9, form);
	QTRY_COMPARE(finished.count(), 2);
	QVERIFY(requests.at(1).startsWith("PATCH /api/v1/admin/chargers/9 "));
	const QJsonObject updateBody = QJsonDocument::fromJson(requests.at(1).split('\n').last()).object();
	QCOMPARE(updateBody.value(QStringLiteral("type")).toString(), QStringLiteral("slow"));
	QCOMPARE(updateBody.value(QStringLiteral("powerKw")).toDouble(), 7.25);
	QCOMPARE(updateBody.value(QStringLiteral("operationalStatus")).toString(),
			 QStringLiteral("offline"));
	QVERIFY(!updateBody.contains(QStringLiteral("occupancyStatus")));
	QVERIFY(!updateBody.contains(QStringLiteral("stationId")));

	client.deleteCharger(9);
	QTRY_COMPARE(finished.count(), 3);
	QVERIFY(requests.at(2).startsWith("DELETE /api/v1/admin/chargers/9 "));
}

void ChargerApiTests::fetchesCompleteStatusOverview() {
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	connect(&server, &QTcpServer::newConnection, this, [&] {
		QTcpSocket *socket = server.nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, socket, [socket] {
			socket->readAll();
			const QByteArray body = R"({"data":{"total":20,"occupancy":{"available":12,"reserved":3,"charging":5},"operational":{"online":17,"fault":2,"offline":1}},"meta":{"requestId":"test"}})";
			const QByteArray response =
				"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
				QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
			socket->write(response);
			socket->disconnectFromHost();
		});
	});

	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
	ops::ChargerStatusSnapshot snapshot;
	bool received = false;
	connect(&client, &ops::ApiClient::chargerStatusFetched, this,
			[&](const ops::ChargerStatusSnapshot &value, const QString &) {
				snapshot = value;
				received = true;
			});
	client.fetchChargerStatus();
	QTRY_VERIFY(received);

	QCOMPARE(snapshot.total, 20);
	QCOMPARE(snapshot.occupancy.size(), 3);
	QCOMPARE(snapshot.operational.size(), 3);
	QCOMPARE(snapshot.occupancy.at(0).status, QStringLiteral("available"));
	QCOMPARE(snapshot.occupancy.at(0).count, 12);
	QCOMPARE(snapshot.operational.at(0).status, QStringLiteral("online"));
	QCOMPARE(snapshot.operational.at(0).count, 17);
}

QTEST_GUILESS_MAIN(ChargerApiTests)

#include "charger_api_tests.moc"
