/** @file
 * @brief 用户管理页通过 HTTP 重新获取新增用户的回归测试。
 */
#include "api/api_client.h"
#include "pages/user_page.h"

#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class UserPageTests : public QObject {
	Q_OBJECT
private slots:
	void refreshFetchesNewUsers();
};

void UserPageTests::refreshFetchesNewUsers() {
	QTcpServer server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	bool registered = false;
	QList<QByteArray> requests;
	connect(&server, &QTcpServer::newConnection, this, [&] {
		auto *socket = server.nextPendingConnection();
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer = QByteArray{}]() mutable {
			buffer += socket->readAll();
			if (!buffer.contains("\r\n\r\n"))
				return;
			requests.append(buffer.left(buffer.indexOf("\r\n")));
			const QByteArray body = registered
										? R"({"data":{"items":[{"id":2,"phone":"13800000002","nickname":"new-user","status":"active"}]},"meta":{"page":1,"hasNext":false}})"
										: R"({"data":{"items":[]},"meta":{"page":1,"hasNext":false}})";
			socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
			socket->disconnectFromHost();
		});
	});
	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
	UserPage page(&client);
	auto *table = page.findChild<QTableWidget *>();
	auto *search = page.findChild<QLineEdit *>();
	QVERIFY(table);
	QVERIFY(search);
	QSignalSpy fetched(&client, &ops::ApiClient::usersFetched);
	page.show();
	QTRY_COMPARE(fetched.count(), 1);
	QCOMPARE(table->rowCount(), 0);
	registered = true;
	search->setText(QStringLiteral(" 138 "));
	QPushButton *refresh = nullptr;
	for (auto *button : page.findChildren<QPushButton *>()) {
		if (button->text() == QStringLiteral("刷新"))
			refresh = button;
	}
	QVERIFY2(refresh, "User list has no refresh button to fetch newly registered users");
	QTest::mouseClick(refresh, Qt::LeftButton);
	QTRY_COMPARE(fetched.count(), 2);
	QCOMPARE(requests.size(), 2);
	QVERIFY(requests.last().contains("/admin/users?"));
	QVERIFY(requests.last().contains("phone=138"));
	QVERIFY(requests.last().contains("page=1"));
	QCOMPARE(table->rowCount(), 1);
	QCOMPARE(table->item(0, 2)->text(), QStringLiteral("new-user"));
	QTest::mouseClick(refresh, Qt::LeftButton);
	QTRY_COMPARE(fetched.count(), 3);
}

QTEST_MAIN(UserPageTests)
#include "user_page_tests.moc"
