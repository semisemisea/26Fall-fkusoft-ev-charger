/**
 * @file http.h
 * @brief 轻量 HTTP/1.1 请求解析、路由分派及线程池 TCP 服务。
 */

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

	/** @brief 解析后的 HTTP 请求值对象；路由分派时补充路径参数。 */
	struct HttpRequest {
		QString method;							///< 用于路由匹配的 HTTP 方法。
		QString path;							///< 已解析的 URL 路径，不含查询参数。
		QUrlQuery query;						///< URL 查询参数。
		QHash<QByteArray, QByteArray> headers;	///< HTTP 头字段；请求头名称由解析器转为小写。
		QByteArray body;						///< 原始正文内容，按媒体类型解释。
		QHash<QString, QString> pathParameters; ///< 路由匹配得到的花括号路径参数。
		QString requestId;						///< 请求追踪 UUID。
	};

	/** @brief 待写入连接的响应值对象，正文可为 JSON 或头像字节。 */
	struct HttpResponse {
		int status = 200;															   ///< HTTP 响应状态码。
		QByteArray contentType = QByteArrayLiteral("application/json; charset=utf-8"); ///< 响应媒体类型，默认 UTF-8 JSON。
		QByteArray body;															   ///< 原始正文内容，按媒体类型解释。
		QHash<QByteArray, QByteArray> headers;										   ///< HTTP 头字段；请求头名称由解析器转为小写。
	};

	/**
	 * @brief 创建包含 data 与请求元数据的 JSON 成功信封。
	 * @param data 返回给调用方或保存用于重放的数据。
	 * @param requestId 本次请求关联标识。
	 * @param status HTTP 响应状态码。
	 * @param extraMeta 需要合并到响应 meta 中的附加字段。
	 * @return 采用 status 状态码的 data/meta JSON 响应。
	 */
	HttpResponse jsonData(const QJsonValue &data, const QString &requestId, int status = 200, const QJsonObject &extraMeta = {});
	/**
	 * @brief 创建包含错误码、消息、详情及请求 ID 的 JSON 错误信封。
	 * @param code API 错误代码。
	 * @param message 面向调用方的错误说明。
	 * @param details 字段级校验详情。
	 * @param requestId 本次请求关联标识。
	 * @param status HTTP 响应状态码。
	 * @return 采用 status 状态码的 error/meta JSON 响应。
	 */
	HttpResponse jsonError(const QString &code, const QString &message, const QJsonObject &details, const QString &requestId, int status);

	/** @brief 按注册顺序匹配方法和路径模式的轻量路由器。 */
	class Router {
	public:
		/// @brief 同步请求处理回调，接收只读请求并返回响应值。
		using Handler = std::function<HttpResponse(const HttpRequest &)>;

		/**
		 * @brief 按注册顺序追加方法、路径模式和处理回调。
		 * @param method HTTP 请求方法。
		 * @param pathPattern 包含花括号参数的路径模式。
		 * @param handler 匹配成功后执行的请求处理回调。
		 */
		void add(const QString &method, const QString &pathPattern, Handler handler);
		/**
		 * @brief 匹配路径并填充参数后调用处理器；区分不存在路径与方法不允许。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse dispatch(HttpRequest request) const;

	private:
		/** @brief 一条请求方法、路径模式与回调的注册记录。 */
		struct Route {
			QString method;		 ///< 用于路由匹配的 HTTP 方法。
			QString pathPattern; ///< 允许包含花括号参数的路径模式。
			Handler handler;	 ///< 匹配后同步调用的处理器。
		};

		QList<Route> m_routes; ///< 按注册顺序保存的路由；服务启动后只读访问。
	};

	/** @brief 监听 TCP 并在线程池处理单次 HTTP 请求的服务器。 */
	class HttpServer final : public QTcpServer {
	public:
		/**
		 * @brief 保存共享路由和请求大小限制，并设置工作线程池。
		 * @param router 接收路由注册或用于分派请求的路由器。
		 * @param jsonBodyLimit 普通请求正文大小上限，单位字节。
		 * @param avatarBodyLimit 头像请求正文大小上限，单位字节。
		 * @param parent Qt 父对象，参与 QObject 生命周期管理。
		 */
		HttpServer(std::shared_ptr<const Router> router, qint64 jsonBodyLimit, qint64 avatarBodyLimit, QObject *parent = nullptr);

		/**
		 * @brief 开始监听指定地址端口，失败时返回套接字错误。
		 * @param address 服务器监听的 IP 地址。
		 * @param port 监听端口。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
		 * @return 监听建立成功返回 true；监听失败返回 false，非空 errorMessage 接收套接字错误。
		 */
		bool start(const QHostAddress &address, quint16 port, QString *errorMessage);
		/**
		 * @brief 停止接收连接并在指定期限内等待工作线程；未结束任务保留其路由依赖。
		 * @param timeoutMs 等待超时，单位毫秒。
		 */
		void stop(int timeoutMs);

	protected:
		/**
		 * @brief 将套接字描述符交给线程池处理，捕获共享路由保证请求期间的生命周期。
		 * @param socketDescriptor 由监听套接字接受的连接描述符。
		 */
		void incomingConnection(qintptr socketDescriptor) override;

	private:
		std::shared_ptr<const Router> m_router; ///< 请求期间共享持有的只读路由器。
		qint64 m_jsonBodyLimit;					///< 普通请求正文上限，单位字节。
		qint64 m_avatarBodyLimit;				///< 头像请求正文上限，单位字节。
		QThreadPool m_workers;					///< 管理请求任务生命周期的工作线程池。
	};

} // namespace Backend

#endif
