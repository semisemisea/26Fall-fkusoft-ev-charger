#ifndef BACKEND_HTTP_H
#define BACKEND_HTTP_H

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QTcpServer>
#include <QThreadPool>
#include <QUrlQuery>

#include <functional>
#include <memory>

namespace Backend {

	struct HttpRequest {
		QString method;
		QString path;
		QUrlQuery query;
		QHash<QByteArray, QByteArray> headers;
		QByteArray body;
		QHash<QString, QString> pathParameters;
		QString requestId;
	};

	struct HttpResponse {
		int status = 200;
		QByteArray contentType = QByteArrayLiteral("application/json; charset=utf-8");
		QByteArray body;
		QHash<QByteArray, QByteArray> headers;
	};

	HttpResponse jsonData(const QJsonValue &data, const QString &requestId, int status = 200, const QJsonObject &extraMeta = {});
	HttpResponse jsonError(const QString &code, const QString &message, const QJsonObject &details, const QString &requestId, int status);

	class Router {
	public:
		using Handler = std::function<HttpResponse(const HttpRequest &)>;

		void add(const QString &method, const QString &pathPattern, Handler handler);
		HttpResponse dispatch(HttpRequest request) const;

	private:
		struct Route {
			QString method;
			QString pathPattern;
			Handler handler;
		};

		QList<Route> m_routes;
	};

	class HttpServer final : public QTcpServer {
	public:
		HttpServer(std::shared_ptr<const Router> router, qint64 jsonBodyLimit, qint64 avatarBodyLimit, QObject *parent = nullptr);

		bool start(const QHostAddress &address, quint16 port, QString *errorMessage);
		void stop(int timeoutMs);

	protected:
		void incomingConnection(qintptr socketDescriptor) override;

	private:
		std::shared_ptr<const Router> m_router;
		qint64 m_jsonBodyLimit;
		qint64 m_avatarBodyLimit;
		QThreadPool m_workers;
	};

} // namespace Backend

#endif
