#include "api/apiclient.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

template <typename T>
concept HasChargerCount = requires(T value) {
	value.chargerCount;
	value.fastCount;
};

template <typename T>
concept HasAddress = requires(T value) {
	value.address;
};

template <typename T>
void configureLegacyChargerCounts(T &form) {
	if constexpr (HasChargerCount<T>) {
		form.chargerCount = 2;
		form.fastCount = 1;
	}
}

class StationApiTests : public QObject {
	Q_OBJECT

private slots:
	void stationFormDoesNotOwnChargers();
	void stationFormDoesNotOwnAddress();
	void createStationSendsOneRequest();
};

void StationApiTests::stationFormDoesNotOwnChargers() {
	QVERIFY(!HasChargerCount<ops::StationForm>);
}

void StationApiTests::stationFormDoesNotOwnAddress() {
	QVERIFY(!HasAddress<ops::StationForm>);
}

void StationApiTests::createStationSendsOneRequest() {
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
			const QByteArray responseBody =
				R"({"data":{"id":5},"meta":{"requestId":"test"}})";
			const QByteArray response =
				"HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: " +
				QByteArray::number(responseBody.size()) + "\r\nConnection: close\r\n\r\n" + responseBody;
			socket->write(response);
			socket->disconnectFromHost();
		});
	});

	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
	QSignalSpy finished(&client, &ops::ApiClient::stationCreated);
	ops::StationForm form;
	form.name = QStringLiteral("软件园充电站");
	form.latitude = 38.889;
	form.longitude = 121.537;
	form.pricePerKwhFen = 98;
	configureLegacyChargerCounts(form);
	client.createStation(form);
	QTRY_COMPARE(finished.count(), 1);
	QTest::qWait(50);

	QCOMPARE(requests.size(), 1);
	QVERIFY(requests.first().startsWith("POST /api/v1/admin/stations "));
	const QJsonObject body = QJsonDocument::fromJson(requests.first().split('\n').last()).object();
	QCOMPARE(body.size(), 4);
	QVERIFY(!body.contains(QStringLiteral("address")));
	QCOMPARE(body.value(QStringLiteral("priceFenPerKwh")).toInt(), 98);
}

QTEST_GUILESS_MAIN(StationApiTests)

#include "station_api_tests.moc"
