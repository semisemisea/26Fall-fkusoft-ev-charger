/**
 * @file http.cpp
 * @brief 轻量 HTTP/1.1 请求解析、路由分派及线程池 TCP 服务。
 */

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

		/// @brief 读取请求头时的缓冲检查上限，单位字节。
		constexpr qsizetype maximumHeaderBytes = 64 * 1024;
		/// @brief 套接字阻塞读写等待期限，单位毫秒。
		constexpr int socketReadTimeoutMs = 30'000;

		/**
		 * @brief 返回支持的 HTTP 状态短语，未知状态使用通用短语。
		 * @param status HTTP 响应状态码。
		 * @return 按上述规则生成的文本或字节结果。
		 */
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

		/**
		 * @brief 按路径分段匹配常量或花括号参数，仅成功时写入全部路径参数。
		 * @param pattern 包含花括号占位符的路径模式。
		 * @param[in] path HTTP 资源路径，不含服务器地址。
		 * @param[out] parameters 仅成功匹配时接收提取出的参数映射，必须有效。
		 * @return 所有路径分段匹配返回 true，并整体替换 parameters；不匹配返回 false 且不修改输出。
		 */
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

		/**
		 * @brief 验证调用方 UUID；缺失或非法时生成新 ID，并单独返回输入有效性。
		 * @param provided 调用方提供的请求 ID 字节。
		 * @param valid 输出原始请求 ID 是否有效；缺失 ID 视为有效。
		 * @return 按上述规则生成的文本或字节结果。
		 */
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

		/**
		 * @brief 构造保留请求 ID 的 HTTP 400 参数错误响应。
		 * @param requestId 本次请求关联标识。
		 * @param message 面向调用方的错误说明。
		 * @return HTTP 400 参数校验错误。
		 */
		HttpResponse badRequest(const QString &requestId, const QString &message = QStringLiteral("请求格式无效")) {
			return jsonError(QStringLiteral("VALIDATION_ERROR"), message, {}, requestId, 400);
		}

		/**
		 * @brief 解析 HTTP/1.1 请求行和头，规范化头名称并拒绝传输编码及非法长度。
		 * @param head 不含正文的 HTTP 请求头字节。
		 * @param[out] request 接收解析的方法、路径、查询和小写头字段；false 时可能只被部分赋值。
		 * @param contentLength 成功时接收解析的正文长度。
		 * @return 请求行、头与正文长度全部符合支持的 HTTP/1.1 格式时返回 true；不支持或非法格式返回 false。
		 */
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

		/**
		 * @brief 写入 HTTP 状态行、请求 ID、长度和关闭连接头，204 响应不发送正文。
		 * @param socket 当前请求的 TCP 连接。
		 * @param response 待发送或解码的 HTTP 响应。
		 * @param requestId 本次请求关联标识。
		 */
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

		/**
		 * @brief 在工作线程接管连接，按头和正文限制读取单次请求、分派并关闭连接。
		 * @param socketDescriptor 由监听套接字接受的连接描述符。
		 * @param router 接收路由注册或用于分派请求的路由器。
		 * @param jsonBodyLimit 普通请求正文大小上限，单位字节。
		 * @param avatarBodyLimit 头像请求正文大小上限，单位字节。
		 */
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

	/**
	 * @brief 创建包含 data 与请求元数据的 JSON 成功信封。
	 * @param data 返回给调用方或保存用于重放的数据。
	 * @param requestId 本次请求关联标识。
	 * @param status HTTP 响应状态码。
	 * @param extraMeta 需要合并到响应 meta 中的附加字段。
	 * @return 采用 status 状态码的 data/meta JSON 响应。
	 */
	HttpResponse jsonData(const QJsonValue &data, const QString &requestId, int status, const QJsonObject &extraMeta) {
		QJsonObject meta = extraMeta;
		meta.insert(QStringLiteral("requestId"), requestId);
		const QJsonObject envelope{
			{QStringLiteral("data"), data},
			{QStringLiteral("meta"), meta},
		};
		return HttpResponse{status, QByteArrayLiteral("application/json; charset=utf-8"), QJsonDocument(envelope).toJson(QJsonDocument::Compact), {}};
	}

	/**
	 * @brief 创建包含错误码、消息、详情及请求 ID 的 JSON 错误信封。
	 * @param code API 错误代码。
	 * @param message 面向调用方的错误说明。
	 * @param details 字段级校验详情。
	 * @param requestId 本次请求关联标识。
	 * @param status HTTP 响应状态码。
	 * @return 采用 status 状态码的 error/meta JSON 响应。
	 */
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

	/**
	 * @brief 按注册顺序追加方法、路径模式和处理回调。
	 * @param method HTTP 请求方法。
	 * @param pathPattern 包含花括号参数的路径模式。
	 * @param handler 匹配成功后执行的请求处理回调。
	 */
	void Router::add(const QString &method, const QString &pathPattern, Handler handler) {
		m_routes.append(Route{method, pathPattern, std::move(handler)});
	}

	/**
	 * @brief 匹配路径并填充参数后调用处理器；区分不存在路径与方法不允许。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
	 */
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

	/**
	 * @brief 保存共享路由和请求大小限制，并设置工作线程池。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param jsonBodyLimit 普通请求正文大小上限，单位字节。
	 * @param avatarBodyLimit 头像请求正文大小上限，单位字节。
	 * @param parent Qt 父对象，参与 QObject 生命周期管理。
	 */
	HttpServer::HttpServer(std::shared_ptr<const Router> router, qint64 jsonBodyLimit, qint64 avatarBodyLimit, QObject *parent)
		: QTcpServer(parent), m_router(std::move(router)), m_jsonBodyLimit(jsonBodyLimit), m_avatarBodyLimit(avatarBodyLimit) {
		m_workers.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
	}

	/**
	 * @brief 开始监听指定地址端口，失败时返回套接字错误。
	 * @param address 服务器监听的 IP 地址。
	 * @param port 监听端口。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
	 * @return 监听建立成功返回 true；监听失败返回 false，非空 errorMessage 接收套接字错误。
	 */
	bool HttpServer::start(const QHostAddress &address, quint16 port, QString *errorMessage) {
		if (listen(address, port)) {
			return true;
		}
		if (errorMessage != nullptr) {
			*errorMessage = errorString();
		}
		return false;
	}

	/**
	 * @brief 停止接收连接并在指定期限内等待工作线程；未结束任务保留其路由依赖。
	 * @param timeoutMs 等待超时，单位毫秒。
	 */
	void HttpServer::stop(int timeoutMs) {
		close();
		m_workers.clear();
		m_workers.waitForDone(timeoutMs);
	}

	/**
	 * @brief 将套接字描述符交给线程池处理，捕获共享路由保证请求期间的生命周期。
	 * @param socketDescriptor 由监听套接字接受的连接描述符。
	 */
	void HttpServer::incomingConnection(qintptr socketDescriptor) {
		const auto router = m_router;
		const qint64 jsonBodyLimit = m_jsonBodyLimit;
		const qint64 avatarBodyLimit = m_avatarBodyLimit;
		m_workers.start(QRunnable::create([socketDescriptor, router, jsonBodyLimit, avatarBodyLimit]() {
			processConnection(socketDescriptor, *router, jsonBodyLimit, avatarBodyLimit);
		}));
	}

} // namespace Backend
