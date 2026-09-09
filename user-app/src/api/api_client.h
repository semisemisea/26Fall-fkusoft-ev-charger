/**
 * @file api_client.h
 * @brief 统一封装用户前端的异步 HTTP 请求、认证头、JSON 信封与网络错误。
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QHttpMultiPart;
class QJsonObject;
class QNetworkAccessManager;
class QUrl;

/**
 * @brief 服务端统一错误结构：页面按 code 做决策，不解析 message 文字
 */
struct ApiError {
	QString code = QStringLiteral("NETWORK_ERROR"); ///< 契约错误码；无 HTTP 响应时默认为 NETWORK_ERROR。
	QString message;								///< 供界面显示的错误原因。
	int httpStatus = 0;								///< HTTP 状态码；未取得 HTTP 响应时为 0。
	QString requestId;								///< 可选的服务端请求追踪标识。
};

/**
 * @brief 唯一网络出口：统一注入 Bearer 令牌与 X-Request-Id，拆 data/meta 信封，失败回调 ApiError
 * @details 请求异步完成；回调在客户端所属线程执行。页面捕获自身时，应保证页面在回调完成前仍存活。
 */
class ApiClient : public QObject {
	Q_OBJECT

public:
	/**
	 * @brief JSON 请求成功回调：data 为业务载荷，meta 为信封元数据；引用仅在本次调用期间有效。
	 */
	using Success = std::function<void(const QJsonValue &data, const QJsonObject &meta)>;
	/**
	 * @brief 失败回调：error 为本次请求的错误快照；引用仅在本次调用期间有效。
	 */
	using Failure = std::function<void(const ApiError &error)>;
	/**
	 * @brief 下载成功回调：payload 为完整响应字节；需要长期保存时由调用方复制。
	 */
	using DownloadSuccess = std::function<void(const QByteArray &payload)>;

	/**
	 * @brief 保存服务根地址并创建由本对象拥有的网络管理器。
	 * @param baseUrl 包含 API 前缀的服务根地址，例如 http://localhost:8080/api/v1。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ApiClient(QString baseUrl, QObject *parent = nullptr);

	/**
	 * @brief 设置/读取访问令牌与服务地址；令牌非空时自动注入 Authorization 头
	 * @param token Bearer 访问令牌；空字符串表示不携带登录凭据。
	 */
	void setAccessToken(const QString &token);
	/**
	 * @brief 读取当前保存的访问令牌。
	 * @return 对象内部访问令牌的只读引用。
	 */
	[[nodiscard]] const QString &accessToken() const { return m_accessToken; } ///< 当前访问令牌，空值表示未登录。
	/**
	 * @brief 读取请求使用的 API 根地址。
	 * @return 对象内部 API 根地址的只读引用。
	 */
	[[nodiscard]] const QString &baseUrl() const { return m_baseUrl; } ///< 包含 API 前缀的服务根地址。

	/**
	 * @brief GET 请求，path 相对 baseUrl
	 * @param path 相对 API 根地址的请求路径，应包含起始斜杠。
	 * @param onSuccess 异步成功回调；JSON 请求接收 data 与 meta，下载请求接收原始字节。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void get(const QString &path, Success onSuccess, Failure onFailure);
	/**
	 * @brief POST JSON 请求
	 * @param path 相对 API 根地址的请求路径，应包含起始斜杠。
	 * @param body 本次发送的 JSON 请求体；发送前同步序列化。
	 * @param onSuccess 异步成功回调；JSON 请求接收 data 与 meta，下载请求接收原始字节。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
	/**
	 * @brief PATCH JSON 请求
	 * @param path 相对 API 根地址的请求路径，应包含起始斜杠。
	 * @param body 本次发送的 JSON 请求体；发送前同步序列化。
	 * @param onSuccess 异步成功回调；JSON 请求接收 data 与 meta，下载请求接收原始字节。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void patch(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
	/**
	 * @brief multipart 上传（如头像），multiPart 由客户端接管生命周期
	 * @param path 相对 API 根地址的请求路径，应包含起始斜杠。
	 * @param multiPart 非空 multipart 请求体；发送后转交响应对象管理生命周期。
	 * @param onSuccess 异步成功回调；JSON 请求接收 data 与 meta，下载请求接收原始字节。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void upload(const QString &path, QHttpMultiPart *multiPart, Success onSuccess, Failure onFailure);
	/**
	 * @brief 下载文件（如充电账单 PDF），url 可为相对地址
	 * @param url 下载地址；相对地址通过服务根地址解析。
	 * @param onSuccess 异步成功回调；JSON 请求接收 data 与 meta，下载请求接收原始字节。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void download(const QUrl &url, DownloadSuccess onSuccess, Failure onFailure);

private:
	/// @brief JSON 请求共用发送器支持的 HTTP 动词。
	enum class Verb {
		Get,  ///< 查询资源，不携带请求体。
		Post, ///< 创建或执行资源操作，额外生成幂等键。
		Patch ///< 局部修改 JSON 资源。
	};

	/**
	 * @brief get/post/patch 共用的发送实现
	 * @param path 相对 API 根地址的请求路径，应包含起始斜杠。
	 * @param verb 需要发送的 HTTP 动词。
	 * @param body 可空的 JSON 请求体指针；非空时在本函数返回前序列化，不保存指针。
	 * @param onSuccess 异步成功回调，接收 JSON 信封中的 data 和 meta。
	 * @param onFailure 异步失败回调，接收可供分支判断的错误码及展示信息。
	 */
	void send(const QString &path, Verb verb, const QJsonObject *body, Success onSuccess, Failure onFailure);

	QString m_baseUrl;							///< 包含 API 前缀的服务根地址。
	QString m_accessToken;						///< 当前访问令牌，空值表示未登录。
	QNetworkAccessManager *m_manager = nullptr; ///< 由 ApiClient 拥有的网络管理器，在客户端所属线程使用。
};
