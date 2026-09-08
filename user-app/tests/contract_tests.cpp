#include "api/ApiClient.h"
#include "models/Charger.h"
#include "models/Order.h"
#include "models/Reservation.h"
#include "models/Station.h"
#include "models/User.h"
#include "views/ChargingView.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class ContractTests : public QObject {
	Q_OBJECT
private slots:
	void parsesMainContract() {
		const auto object = [](const QByteArray &json) { return QJsonDocument::fromJson(json).object(); };
		auto charger = Charger::fromJson(object(R"({"id":17,"stationId":3,"type":"fast","operationalStatus":"online","occupancyStatus":"reserved"})"));
		QCOMPARE(charger.code, QStringLiteral("17"));
		QCOMPARE(charger.status, QStringLiteral("reserved"));
		charger = Charger::fromJson(object(R"({"id":17,"operationalStatus":"fault","occupancyStatus":"available"})"));
		QCOMPARE(charger.status, QStringLiteral("fault"));
		const auto order = Order::fromJson(object(R"({"id":203,"stationId":3,"chargerId":17,"stoppedAt":"2026-09-01T09:00:00Z","energyKwh":1300.25,"unitPriceFenPerKwh":98,"amountFen":127425})"));
		QCOMPARE(order.orderNo, QStringLiteral("203"));
		QCOMPARE(order.chargerCode, QStringLiteral("17"));
		QCOMPARE(order.endedAt, QStringLiteral("2026-09-01T09:00:00Z"));
		QCOMPARE(order.energyKwh, 1300.25);
		QCOMPARE(order.amountFen, 127425LL);
		const auto reservation = Reservation::fromJson(object(R"({"id":81,"stationId":3,"chargerId":17,"createdAt":"2026-09-01T08:32:00Z","expiresAt":"2026-09-01T09:02:00Z"})"));
		QCOMPARE(reservation.startAt, QStringLiteral("2026-09-01T08:32:00Z"));
		QCOMPARE(reservation.chargerCode, QStringLiteral("17"));
		QVERIFY(reservation.expiresAt.isValid());
		QCOMPARE(Station::fromJson(object(R"({"priceFenPerKwh":98})")).pricePerKwhFen, 98LL);
		QCOMPARE(User::fromJson(object(R"({"hasAvatar":true})")).avatarUrl, QStringLiteral("/me/avatar"));
		QVERIFY(User::fromJson(object(R"({"hasAvatar":false})")).avatarUrl.isEmpty());
	}

	void chargingDisplaysChargerTypeAndUncappedMeter() {
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		connect(&server, &QTcpServer::newConnection, this, [&] {
			auto *socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, socket, [socket] {
				auto request = socket->property("request").toByteArray() + socket->readAll();
				socket->setProperty("request", request);
				if (!request.contains("\r\n\r\n"))
					return;
				const QByteArray body = request.startsWith("GET /api/v1/chargers/17 ")
											? R"({"data":{"id":17,"type":"fast","operationalStatus":"online","occupancyStatus":"charging"}})"
											: R"({"data":{"id":203,"chargerId":17,"status":"charging","energyKwh":1300.5,"unitPriceFenPerKwh":98,"amountFen":127449}})";
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		});
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		ChargingView view(api);
		view.show();
		Order order;
		order.id = 203;
		order.chargerId = 17;
		order.energyKwh = 1300.5;
		order.chargerCode = QStringLiteral("17");
		view.open(order);
		const auto hasText = [&view](const QString &text) {
			for (const auto *label : view.findChildren<QLabel *>()) {
				if (label->text().contains(text))
					return true;
			}
			return false;
		};
		QTRY_VERIFY(hasText(QStringLiteral("快充")));
		QVERIFY(hasText(QStringLiteral("1300.5")));
		QVERIFY(hasText(QStringLiteral("kWh")));
		view.hide();
	}
};

QTEST_MAIN(ContractTests)
#include "contract_tests.moc"
