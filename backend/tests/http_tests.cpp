#include "backend/database.h"
#include "backend/http.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

class HttpTests : public QObject {
	Q_OBJECT

private slots:
	void servesHealthWithRequestId();
	void distinguishesMissingPathAndMethod();
	void rejectsInvalidRequestId();
};

namespace {

	struct ReplyData {
		int status = 0;
		QByteArray requestId;
		QJsonObject json;
	};

	ReplyData get(const QUrl &url, const QByteArray &requestId = {}) {
		QNetworkAccessManager manager;
		QNetworkRequest request(url);
		if (!requestId.isEmpty()) {
			request.setRawHeader(QByteArrayLiteral("X-Request-Id"), requestId);
		}
		QNetworkReply *reply = manager.get(request);
		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QTimer::singleShot(5000, &loop, &QEventLoop::quit);
		loop.exec();
		ReplyData result;
		result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		result.requestId = reply->rawHeader(QByteArrayLiteral("X-Request-Id"));
		result.json = QJsonDocument::fromJson(reply->readAll()).object();
		reply->deleteLater();
		return result;
	}

	struct RunningServer {
		QTemporaryDir directory;
		std::shared_ptr<Backend::Database> database;
		std::shared_ptr<Backend::Router> router;
		std::unique_ptr<Backend::HttpServer> server;

		RunningServer()
			: database(std::make_shared<Backend::Database>(directory.filePath(QStringLiteral("test.sqlite3")), 5000)), router(std::make_shared<Backend::Router>()) {
			QString error;
			if (!database->initialize(QDateTime::currentDateTimeUtc(), QString(), &error)) {
				qFatal("Database setup failed: %s", qPrintable(error));
			}
			router->add(QStringLiteral("GET"), QStringLiteral("/health"), [database = database](const Backend::HttpRequest &request) {
				QString error;
				const bool healthy = database->withConnection([](QSqlDatabase &connection, QString *operationError) {
					QSqlQuery query(connection);
					if (query.exec(QStringLiteral("SELECT 1")) && query.next()) {
						return true;
					}
					*operationError = query.lastError().text();
					return false;
				},
															  &error);
				return healthy
						   ? Backend::jsonData(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}}, request.requestId)
						   : Backend::jsonError(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("服务暂不可用"), {}, request.requestId, 503);
			});
			server = std::make_unique<Backend::HttpServer>(router, 1024, 2048);
			if (!server->start(QHostAddress::LocalHost, 0, &error)) {
				qFatal("Server setup failed: %s", qPrintable(error));
			}
		}

		~RunningServer() {
			server->stop(1000);
		}

		QUrl url(const QString &path) const {
			return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server->serverPort()).arg(path));
		}
	};

} // namespace

void HttpTests::servesHealthWithRequestId() {
	RunningServer running;
	const QByteArray requestId = QByteArrayLiteral("0c7d4f5e-7f6e-4d13-9a1c-c4b4b6725a80");
	const ReplyData reply = get(running.url(QStringLiteral("/health")), requestId);
	QVERIFY2(reply.status == 200, QJsonDocument(reply.json).toJson(QJsonDocument::Compact).constData());
	QCOMPARE(reply.requestId, requestId);
	QCOMPARE(reply.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
	QCOMPARE(reply.json.value(QStringLiteral("meta")).toObject().value(QStringLiteral("requestId")).toString(), QString::fromLatin1(requestId));
}

void HttpTests::distinguishesMissingPathAndMethod() {
	RunningServer running;
	const ReplyData missing = get(running.url(QStringLiteral("/missing")));
	QCOMPARE(missing.status, 404);
	QCOMPARE(missing.json.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("NOT_FOUND"));

	QNetworkAccessManager manager;
	QNetworkRequest request(running.url(QStringLiteral("/health")));
	QNetworkReply *networkReply = manager.post(request, QByteArray());
	QEventLoop loop;
	QObject::connect(networkReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	QCOMPARE(networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 405);
	networkReply->deleteLater();
}

void HttpTests::rejectsInvalidRequestId() {
	RunningServer running;
	const ReplyData reply = get(running.url(QStringLiteral("/health")), QByteArrayLiteral("not-a-uuid"));
	QCOMPARE(reply.status, 400);
	QCOMPARE(reply.json.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("VALIDATION_ERROR"));
	QVERIFY(!reply.requestId.isEmpty());
}

QTEST_MAIN(HttpTests)

#include "http_tests.moc"
