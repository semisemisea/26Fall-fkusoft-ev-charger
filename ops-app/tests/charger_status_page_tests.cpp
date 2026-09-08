/** @file
 * @brief 管理员前端回归测试；用本地响应或直接发送信号隔离真实服务。
 */
#include "api/apiclient.h"
#include "pages/chargerstatuspage.h"

#include <QLabel>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

/// @brief 电桩统计页渲染与显示刷新回归测试。
class ChargerStatusPageTests : public QObject {
	Q_OBJECT

private slots:
	/// @brief 直接注入快照信号，检查两张表分别显示占用和运维状态。
	void rendersIndependentStatusDimensions();
	/// @brief 本地服务计数验证状态页隐藏再显示会重新发起请求。
	void refreshesWheneverShown();
};

/// @brief 直接注入快照信号，检查两张表分别显示占用和运维状态。

void ChargerStatusPageTests::rendersIndependentStatusDimensions() {
	ops::ApiClient client;
	ChargerStatusPage page(&client);

	auto *occupancy =
		page.findChild<QTableWidget *>(QStringLiteral("occupancyStatusTable"));
	auto *operational =
		page.findChild<QTableWidget *>(QStringLiteral("operationalStatusTable"));
	auto *total = page.findChild<QLabel *>(QStringLiteral("chargerTotalLabel"));
	QVERIFY(occupancy);
	QVERIFY(operational);
	QVERIFY(total);

	ops::ChargerStatusSnapshot snapshot;
	snapshot.total = 4;
	snapshot.occupancy = {
		{QStringLiteral("available"), 2, 0.5},
		{QStringLiteral("reserved"), 1, 0.25},
		{QStringLiteral("charging"), 1, 0.25},
	};
	snapshot.operational = {
		{QStringLiteral("online"), 3, 0.75},
		{QStringLiteral("fault"), 1, 0.25},
		{QStringLiteral("offline"), 0, 0.0},
	};
	client.chargerStatusFetched(snapshot, {});

	QCOMPARE(total->text(), QStringLiteral("电桩总数: 4"));
	QCOMPARE(occupancy->rowCount(), 3);
	QCOMPARE(operational->rowCount(), 3);
	QCOMPARE(occupancy->item(0, 0)->text(), QStringLiteral("闲置"));
	QCOMPARE(operational->item(0, 0)->text(), QStringLiteral("在线"));
}

/// @brief 本地服务计数验证状态页隐藏再显示会重新发起请求。

void ChargerStatusPageTests::refreshesWheneverShown() {
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	int requestCount = 0;
	connect(&server, &QTcpServer::newConnection, this, [&] {
		QTcpSocket *socket = server.nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestCount] {
			socket->readAll();
			++requestCount;
			const QByteArray body = R"({"data":{"total":1,"occupancy":{"available":1,"reserved":0,"charging":0},"operational":{"online":1,"fault":0,"offline":0}},"meta":{"requestId":"test"}})";
			const QByteArray response =
				"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
				QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
			socket->write(response);
			socket->disconnectFromHost();
		});
	});

	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
	ChargerStatusPage page(&client);
	page.show();
	QTRY_COMPARE(requestCount, 1);
	page.hide();
	QCoreApplication::processEvents();
	page.show();
	QTRY_COMPARE(requestCount, 2);
}

QTEST_MAIN(ChargerStatusPageTests)

#include "charger_status_page_tests.moc"
