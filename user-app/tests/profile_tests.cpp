#include "views/ProfileView.h"

#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

class ProfileTests : public QObject {
	Q_OBJECT
private slots:
	void logoutRevokesToken_data() {
		QTest::addColumn<int>("status");
		QTest::newRow("success") << 204;
		QTest::newRow("expired") << 401;
		QTest::newRow("server-error") << 500;
		QTest::newRow("cancel") << 0;
	}
	void logoutRevokesToken() {
		QFETCH(int, status);
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QByteArray received;
		QTcpSocket *pending = nullptr;
		connect(&server, &QTcpServer::newConnection, this, [&] {
			pending = server.nextPendingConnection();
			connect(pending, &QTcpSocket::readyRead, pending, [&] { received += pending->readAll(); });
		});
		Session session;
		session.signIn(User{}, QStringLiteral("test-token"));
		ApiClient api(QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
		api.setAccessToken(session.accessToken());
		connect(&session, &Session::signedOut, &api, [&] { api.setAccessToken({}); });
		ProfileView view(session, api);
		view.show();
		auto *button = view.findChild<QPushButton *>(QStringLiteral("outlineDangerButton"));
		QVERIFY(button);
		QTimer::singleShot(0, &view, [&] {
			auto *dialog = view.findChild<QMessageBox *>();
			QVERIFY(dialog);
			dialog->button(status == 0 ? QMessageBox::No : QMessageBox::Yes)->click();
		});
		QTest::mouseClick(button, Qt::LeftButton);
		if (status == 0) {
			QTest::qWait(50);
			QVERIFY(received.isEmpty());
			QVERIFY(session.isLoggedIn());
			return;
		}
		QTRY_VERIFY_WITH_TIMEOUT(received.contains("\r\n\r\n"), 1000);
		QVERIFY(received.startsWith("POST /api/v1/auth/logout "));
		QVERIFY(received.contains("Authorization: Bearer test-token"));
		QVERIFY(session.isLoggedIn());
		QVERIFY(!button->isEnabled());
		pending->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
		pending->disconnectFromHost();
		QTRY_VERIFY(button->isEnabled());
		QCOMPARE(session.isLoggedIn(), status == 500);
		QCOMPARE(api.accessToken().isEmpty(), status != 500);
	}
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
