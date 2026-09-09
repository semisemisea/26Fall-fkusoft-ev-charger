#include "views/info_pages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class TransactionsTests : public QObject {
	Q_OBJECT
private slots:
	void rendersTransactionDirection_data() {
		QTest::addColumn<QString>("type");
		QTest::addColumn<qint64>("amount");
		QTest::addColumn<QString>("text");
		QTest::addColumn<QString>("style");
		QTest::newRow("top-up") << QStringLiteral("top_up") << qint64(1234) << QStringLiteral("+￥12.34") << QStringLiteral("txIncome");
		QTest::newRow("debit") << QStringLiteral("charge_debit") << qint64(1234) << QStringLiteral("-￥12.34") << QStringLiteral("txExpense");
		QTest::newRow("zero-debit") << QStringLiteral("charge_debit") << qint64(0) << QStringLiteral("-￥0.00") << QStringLiteral("txExpense");
	}
	void rendersTransactionDirection() {
		QFETCH(QString, type);
		QFETCH(qint64, amount);
		QFETCH(QString, text);
		QFETCH(QString, style);
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QByteArray request;
		const QByteArray body = QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{{"type", type}, {"amountFen", amount}, {"balanceAfterFen", 5000}}}}}).toJson(QJsonDocument::Compact);
		connect(&server, &QTcpServer::newConnection, this, [&] {
			auto *socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				request += socket->readAll();
				if (!request.contains("\r\n\r\n"))
					return;
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		});
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		TransactionsView view(api);
		view.show();
		QTRY_VERIFY(view.findChild<QLabel *>(style));
		QVERIFY(request.startsWith("GET /api/v1/me/wallet/transactions "));
		QCOMPARE(view.findChild<QLabel *>(style)->text(), text);
	}
};
QTEST_MAIN(TransactionsTests)
#include "transactions_tests.moc"
