#include "backend/http.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QRunnable>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QUuid>

#include <limits>

namespace Backend {
	namespace {

		constexpr qsizetype maximumHeaderBytes = 64 * 1024;
		constexpr int socketReadTimeoutMs = 30'000;

		QByteArray reasonPhrase(int status) {
			switch (status) {
			case 200:
				return QByteArrayLiteral("OK");
			case 201:
				return QByteArrayLiteral("Created");
			case 204:
				return QByteArrayLiteral("No Content");
			case 400:
				return QByteArrayLiteral("Bad Request");
			case 401:
				return QByteArrayLiteral("Unauthorized");
			case 403:
				return QByteArrayLiteral("Forbidden");
			case 404:
				return QByteArrayLiteral("Not Found");
			case 405:
				return QByteArrayLiteral("Method Not Allowed");
			case 409:
				return QByteArrayLiteral("Conflict");
			case 413:
				return QByteArrayLiteral("Payload Too Large");
			case 415:
				return QByteArrayLiteral("Unsupported Media Type");
			case 422:
				return QByteArrayLiteral("Unprocessable Content");
			case 500:
				return QByteArrayLiteral("Internal Server Error");
			case 502:
				return QByteArrayLiteral("Bad Gateway");
			case 503:
				return QByteArrayLiteral("Service Unavailable");
			default:
				return QByteArrayLiteral("Response");
			}
		}

		bool matchPath(const QString &pattern, const QString &path, QHash<QString, QString> *parameters) {
			const QStringList patternParts = pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts);
			const QStringList pathParts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
			if (patternParts.size() != pathParts.size()) {
				return false;
			}
			QHash<QString, QString> matched;
			for (qsizetype index = 0; index < patternParts.size(); ++index) {
				const QString &patternPart = patternParts[index];
				if (patternPart.startsWith(QLatin1Char('{')) && patternPart.endsWith(QLatin1Char('}'))) {
					matched.insert(patternPart.mid(1, patternPart.size() - 2), pathParts[index]);
				} else if (patternPart != pathParts[index]) {
					return false;
				}
			}
			*parameters = matched;
			return true;
		}

		QString validOrGeneratedRequestId(const QByteArray &provided, bool *valid) {
			static const QRegularExpression uuidPattern(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
			const QString text = QString::fromLatin1(provided);
			if (provided.isEmpty()) {
				*valid = true;
				return QUuid::createUuid().toString(QUuid::WithoutBraces);
			}
			*valid = uuidPattern.match(text).hasMatch();
			return *valid ? text : QUuid::createUuid().toString(QUuid::WithoutBraces);
		}

		HttpResponse badRequest(const QString &requestId, const QString &message = QStringLiteral("请求格式无效")) {
			return jsonError(QStringLiteral("VALIDATION_ERROR"), message, {}, requestId, 400);
		}

		bool parseRequestHead(const QByteArray &head, HttpRequest *request, qint64 *contentLength) {
			const QList<QByteArray> lines = head.split('\n');
			if (lines.isEmpty()) {
				return false;
			}
			QByteArray requestLine = lines.first().trimmed();
			const QList<QByteArray> requestParts = requestLine.split(' ');
			if (requestParts.size() != 3 || requestParts[2] != QByteArrayLiteral("HTTP/1.1")) {
				return false;
			}
			request->method = QString::fromLatin1(requestParts[0]);
			const QUrl url = QUrl::fromEncoded(requestParts[1], QUrl::StrictMode);
			if (!url.isValid() || !url.isRelative() || !url.path().startsWith(QLatin1Char('/'))) {
				return false;
			}
			request->path = url.path();
			request->query = QUrlQuery(url);
			for (qsizetype index = 1; index < lines.size(); ++index) {
				const QByteArray line = lines[index].trimmed();
				if (line.isEmpty()) {
					continue;
				}
				const qsizetype separator = line.indexOf(':');
				if (separator <= 0) {
					return false;
				}
				request->headers.insert(line.left(separator).trimmed().toLower(), line.mid(separator + 1).trimmed());
			}
			if (request->headers.contains(QByteArrayLiteral("transfer-encoding"))) {
				return false;
			}
			*contentLength = 0;
			if (request->headers.contains(QByteArrayLiteral("content-length"))) {
				bool ok = false;
				*contentLength = request->headers.value(QByteArrayLiteral("content-length")).toLongLong(&ok);
				if (!ok || *contentLength < 0) {
					return false;
				}
			}
			return true;
		}

		void writeResponse(QTcpSocket &socket, HttpResponse response, const QString &requestId) {
			response.headers.insert(QByteArrayLiteral("X-Request-Id"), requestId.toLatin1());
			QByteArray wire = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(response.status) + ' ' + reasonPhrase(response.status) + QByteArrayLiteral("\r\n");
			if (response.status != 204) {
				response.headers.insert(QByteArrayLiteral("Content-Type"), response.contentType);
				response.headers.insert(QByteArrayLiteral("Content-Length"), QByteArray::number(response.body.size()));
			} else {
				response.body.clear();
			}
			response.headers.insert(QByteArrayLiteral("Connection"), QByteArrayLiteral("close"));
			for (auto iterator = response.headers.cbegin(); iterator != response.headers.cend(); ++iterator) {
				wire += iterator.key() + QByteArrayLiteral(": ") + iterator.value() + QByteArrayLiteral("\r\n");
			}
			wire += QByteArrayLiteral("\r\n") + response.body;
			socket.write(wire);
			socket.waitForBytesWritten(socketReadTimeoutMs);
			socket.disconnectFromHost();
		}

		void processConnection(qintptr socketDescriptor, const Router &router, qint64 jsonBodyLimit, qint64 avatarBodyLimit) {
			QTcpSocket socket;
			if (!socket.setSocketDescriptor(socketDescriptor)) {
				return;
			}
			QByteArray bytes;
			qsizetype headEnd = -1;
			while ((headEnd = bytes.indexOf(QByteArrayLiteral("\r\n\r\n"))) < 0) {
				if (bytes.size() > maximumHeaderBytes || (!socket.bytesAvailable() && !socket.waitForReadyRead(socketReadTimeoutMs))) {
					return;
				}
				bytes += socket.readAll();
			}

			HttpRequest request;
			qint64 contentLength = 0;
			bool requestIdValid = false;
			if (!parseRequestHead(bytes.left(headEnd), &request, &contentLength)) {
				const QString requestId = validOrGeneratedRequestId({}, &requestIdValid);
				writeResponse(socket, badRequest(requestId), requestId);
				return;
			}
			request.requestId = validOrGeneratedRequestId(request.headers.value(QByteArrayLiteral("x-request-id")), &requestIdValid);
			if (!requestIdValid) {
				writeResponse(socket, badRequest(request.requestId, QStringLiteral("X-Request-Id 必须是 UUID")), request.requestId);
				return;
			}
			const bool avatarRequest = request.path == QStringLiteral("/api/v1/me/avatar");
			const qint64 bodyLimit = avatarRequest ? avatarBodyLimit : jsonBodyLimit;
			if (contentLength > bodyLimit) {
				writeResponse(socket, jsonError(QStringLiteral("PAYLOAD_TOO_LARGE"), QStringLiteral("请求正文过大"), {}, request.requestId, 413), request.requestId);
				return;
			}
			request.body = bytes.mid(headEnd + 4);
			while (request.body.size() < contentLength) {
				if (!socket.bytesAvailable() && !socket.waitForReadyRead(socketReadTimeoutMs)) {
					writeResponse(socket, badRequest(request.requestId), request.requestId);
					return;
				}
				request.body += socket.read(contentLength - request.body.size());
			}
			if (request.body.size() > contentLength) {
				request.body.truncate(contentLength);
			}
			const QString requestId = request.requestId;
			try {
				writeResponse(socket, router.dispatch(std::move(request)), requestId);
			} catch (...) {
				writeResponse(socket, jsonError(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("服务内部错误"), {}, requestId, 500), requestId);
			}
		}

	} // namespace

	HttpResponse jsonData(const QJsonValue &data, const QString &requestId, int status, const QJsonObject &extraMeta) {
		QJsonObject meta = extraMeta;
		meta.insert(QStringLiteral("requestId"), requestId);
		const QJsonObject envelope{
			{QStringLiteral("data"), data},
			{QStringLiteral("meta"), meta},
		};
		return HttpResponse{status, QByteArrayLiteral("application/json; charset=utf-8"), QJsonDocument(envelope).toJson(QJsonDocument::Compact), {}};
	}

	HttpResponse jsonError(const QString &code, const QString &message, const QJsonObject &details, const QString &requestId, int status) {
		const QJsonObject envelope{
			{QStringLiteral("error"), QJsonObject{
										  {QStringLiteral("code"), code},
										  {QStringLiteral("message"), message},
										  {QStringLiteral("details"), details},
									  }},
			{QStringLiteral("meta"), QJsonObject{{QStringLiteral("requestId"), requestId}}},
		};
		return HttpResponse{status, QByteArrayLiteral("application/json; charset=utf-8"), QJsonDocument(envelope).toJson(QJsonDocument::Compact), {}};
	}

	void Router::add(const QString &method, const QString &pathPattern, Handler handler) {
		m_routes.append(Route{method, pathPattern, std::move(handler)});
	}

	HttpResponse Router::dispatch(HttpRequest request) const {
		bool pathExists = false;
		for (const Route &route : m_routes) {
			QHash<QString, QString> parameters;
			if (!matchPath(route.pathPattern, request.path, &parameters)) {
				continue;
			}
			pathExists = true;
			if (route.method == request.method) {
				request.pathParameters = parameters;
				return route.handler(request);
			}
		}
		return pathExists
				   ? jsonError(QStringLiteral("METHOD_NOT_ALLOWED"), QStringLiteral("请求方法不受支持"), {}, request.requestId, 405)
				   : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("请求路径不存在"), {}, request.requestId, 404);
	}

	HttpServer::HttpServer(std::shared_ptr<const Router> router, qint64 jsonBodyLimit, qint64 avatarBodyLimit, QObject *parent)
		: QTcpServer(parent), m_router(std::move(router)), m_jsonBodyLimit(jsonBodyLimit), m_avatarBodyLimit(avatarBodyLimit) {
		m_workers.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
	}

	bool HttpServer::start(const QHostAddress &address, quint16 port, QString *errorMessage) {
		if (listen(address, port)) {
			return true;
		}
		if (errorMessage != nullptr) {
			*errorMessage = errorString();
		}
		return false;
	}

	void HttpServer::stop(int timeoutMs) {
		close();
		m_workers.clear();
		m_workers.waitForDone(timeoutMs);
	}

	void HttpServer::incomingConnection(qintptr socketDescriptor) {
		const auto router = m_router;
		const qint64 jsonBodyLimit = m_jsonBodyLimit;
		const qint64 avatarBodyLimit = m_avatarBodyLimit;
		m_workers.start(QRunnable::create([socketDescriptor, router, jsonBodyLimit, avatarBodyLimit]() {
			processConnection(socketDescriptor, *router, jsonBodyLimit, avatarBodyLimit);
		}));
	}

} // namespace Backend
