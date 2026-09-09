#include "views/ProfileView.h"

#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

class ProfileTests : public QObject {
	Q_OBJECT
private slots:
	void editsNickname() {
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QByteArray received;
		connect(&server, &QTcpServer::newConnection, this, [&] {
			auto *socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				received += socket->readAll();
				if (!received.contains("\r\n\r\n") || !received.endsWith('}'))
					return;
				const QByteArray body = R"({"data":{"nickname":"新昵称","phone":"13800000000"}})";
				socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		});
		Session session;
		User user;
		user.nickname = QStringLiteral("旧昵称");
		session.signIn(user, QStringLiteral("test-token"));
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		api.setAccessToken(session.accessToken());
		ProfileView view(session, api);
		view.show();
		QPushButton *edit = nullptr;
		for (auto *button : view.findChildren<QPushButton *>()) {
			if (button->text() == QStringLiteral("修改昵称"))
				edit = button;
		}
		QVERIFY(edit);
		QVERIFY(edit->isVisible());
		QTimer::singleShot(0, &view, [&] {
			auto *dialog = view.findChild<QInputDialog *>();
			QVERIFY(dialog);
			dialog->setTextValue(QStringLiteral("  新昵称  "));
			dialog->accept();
		});
		QTest::mouseClick(edit, Qt::LeftButton);
		QTRY_COMPARE(session.user().nickname, QStringLiteral("新昵称"));
		QVERIFY(received.startsWith("PATCH /api/v1/me "));
		QVERIFY(received.contains("Authorization: Bearer test-token"));
		QCOMPARE(QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n") + 4)).object().value("nickname").toString(), QStringLiteral("新昵称"));
		QCOMPARE(view.findChild<QLabel *>(QStringLiteral("profileName"))->text(), QStringLiteral("新昵称"));
	}
};
QTEST_MAIN(ProfileTests)
#include "profile_tests.moc"
