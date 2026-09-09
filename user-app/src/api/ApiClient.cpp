/**
 * @file ApiClient.cpp
 * @brief 统一封装用户前端的异步 HTTP 请求、认证头、JSON 信封与网络错误。
 */
#include "ApiClient.h"
#include <evcharger/logging.h>

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUuid>

Q_LOGGING_CATEGORY(userNetwork, "evcharger.user.network", QtInfoMsg)

namespace {
	/// @brief 统一网络请求的传输超时，单位毫秒。
	constexpr int kRequestTimeoutMs = 10'000;
} // namespace

/**
 * @details 移动保存服务地址并创建同线程网络管理器，由 QObject 父子树负责释放。
 */
ApiClient::ApiClient(QString baseUrl, QObject *parent)
	: QObject(parent), m_baseUrl(std::move(baseUrl)), m_manager(new QNetworkAccessManager(this)) {
	setObjectName(QStringLiteral("userApiClient"));
	m_manager->setObjectName(QStringLiteral("userNetworkManager"));
	EV_LOG_INFO(userNetwork, this) << "API client initialized; timeout_ms=" << kRequestTimeoutMs;
}

/**
 * @details 保存令牌，后续请求自动携带
 */
void ApiClient::setAccessToken(const QString &token) {
	m_accessToken = token;
	EV_LOG_DEBUG(userNetwork, this) << "Authentication state updated; authenticated=" << !token.isEmpty();
}

/**
 * @details 无请求体的 GET
 */
void ApiClient::get(const QString &path, Success onSuccess, Failure onFailure) {
	send(path, Verb::Get, nullptr, std::move(onSuccess), std::move(onFailure));
}

/**
 * @details 带 JSON 请求体的 POST
 */
void ApiClient::post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure) {
	send(path, Verb::Post, &body, std::move(onSuccess), std::move(onFailure));
}

/**
 * @details 带 JSON 请求体的 PATCH（sendCustomRequest 实现）
 */
void ApiClient::patch(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure) {
	send(path, Verb::Patch, &body, std::move(onSuccess), std::move(onFailure));
}

/**
 * @details 统一发送入口：注入令牌与请求 ID，按契约拆 data/meta 信封，失败解析为 ApiError
 */
void ApiClient::send(const QString &path, Verb verb, const QJsonObject *body, Success onSuccess, Failure onFailure) {
	QNetworkRequest request{QUrl(m_baseUrl + path)};
	request.setTransferTimeout(kRequestTimeoutMs);
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
	// 幂等键按本次调用生成；客户端没有自动重试或跨调用复用键的机制。
	if (verb == Verb::Post) {
		request.setRawHeader("Idempotency-Key", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
	}
	if (!m_accessToken.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
	}

	QByteArray payload;
	if (body) {
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
	}

	const QByteArray requestId = request.rawHeader("X-Request-Id");
	const QString endpoint = request.url().path();
	EV_LOG_DEBUG(userNetwork, this) << "Sending request; request_id=" << requestId << "method=" << (verb == Verb::Get ? "GET" : verb == Verb::Post ? "POST"
																																				   : "PATCH")
									<< "endpoint=" << endpoint;
	QNetworkReply *reply = nullptr;
	switch (verb) {
	case Verb::Get:
		reply = m_manager->get(request);
		break;
	case Verb::Post:
		reply = m_manager->post(request, payload);
		break;
	case Verb::Patch:
		reply = m_manager->sendCustomRequest(request, "PATCH", payload);
		break;
	}

	connect(reply, &QNetworkReply::finished, this,
			[this, reply, requestId, endpoint, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)] {
				// 延迟释放保证本轮 finished 回调仍可安全读取响应属性和字节。
				reply->deleteLater();

				const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				if (status == 204 && reply->error() == QNetworkReply::NoError) {
					onSuccess(QJsonValue(), QJsonObject());
					return;
				}
				QJsonParseError parseError;
				const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
				const QJsonObject root = document.object();
				if (status != 0 && (parseError.error != QJsonParseError::NoError || !document.isObject())) {
					EV_LOG_WARNING(userNetwork, this) << "Invalid JSON response envelope; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status << "parse_error=" << int(parseError.error);
				}

				if (status == 0) {
					EV_LOG_WARNING(userNetwork, this) << "Transport failure; request_id=" << requestId << "endpoint=" << endpoint << "network_error=" << int(reply->error());
					ApiError error;
					error.message = reply->errorString();
					onFailure(error);
					return;
				}
				if (reply->error() == QNetworkReply::NoError) {
					EV_LOG_DEBUG(userNetwork, this) << "Request completed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status;
					onSuccess(root.value(QLatin1String("data")), root.value(QLatin1String("meta")).toObject());
					return;
				}

				ApiError error;
				error.httpStatus = status;
				EV_LOG_WARNING(userNetwork, this) << "HTTP request failed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status << "network_error=" << int(reply->error());
				const QJsonObject err = root.value(QLatin1String("error")).toObject();
				error.code = err.value(QLatin1String("code")).toString(QStringLiteral("HTTP_%1").arg(status));
				error.message = err.value(QLatin1String("message")).toString(reply->errorString());
				error.requestId = root.value(QLatin1String("meta")).toObject().value(QLatin1String("requestId")).toString();
				onFailure(error);
			});
}

/**
 * @details multipart 上传：multiPart 挂到 reply 下随响应一起释放
 */
void ApiClient::upload(const QString &path, QHttpMultiPart *multiPart, Success onSuccess, Failure onFailure) {
	QNetworkRequest request{QUrl(m_baseUrl + path)};
	request.setTransferTimeout(kRequestTimeoutMs);
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
	if (!m_accessToken.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
	}

	const QByteArray requestId = request.rawHeader("X-Request-Id");
	const QString endpoint = request.url().path();
	EV_LOG_INFO(userNetwork, this) << "Uploading multipart content; request_id=" << requestId << "endpoint=" << endpoint;
	QNetworkReply *reply = m_manager->post(request, multiPart);
	multiPart->setParent(reply);

	connect(reply, &QNetworkReply::finished, this,
			[this, reply, requestId, endpoint, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)] {
				// 延迟释放保证本轮 finished 回调仍可安全读取响应属性和字节。
				reply->deleteLater();

				const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				QJsonParseError parseError;
				const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
				const QJsonObject root = document.object();
				if (status != 0 && (parseError.error != QJsonParseError::NoError || !document.isObject())) {
					EV_LOG_WARNING(userNetwork, this) << "Invalid JSON response envelope; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status << "parse_error=" << int(parseError.error);
				}

				if (status == 0) {
					EV_LOG_WARNING(userNetwork, this) << "Transport failure; request_id=" << requestId << "endpoint=" << endpoint << "network_error=" << int(reply->error());
					ApiError error;
					error.message = reply->errorString();
					onFailure(error);
					return;
				}
				if (reply->error() == QNetworkReply::NoError) {
					EV_LOG_DEBUG(userNetwork, this) << "Request completed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status;
					onSuccess(root.value(QLatin1String("data")), root.value(QLatin1String("meta")).toObject());
					return;
				}

				ApiError error;
				error.httpStatus = status;
				EV_LOG_WARNING(userNetwork, this) << "HTTP request failed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status << "network_error=" << int(reply->error());
				const QJsonObject err = root.value(QLatin1String("error")).toObject();
				error.code = err.value(QLatin1String("code")).toString(QStringLiteral("HTTP_%1").arg(status));
				error.message = err.value(QLatin1String("message")).toString(reply->errorString());
				onFailure(error);
			});
}

/**
 * @details 下载二进制内容：相对地址按 baseUrl 解析，成功回调原始字节流
 */
void ApiClient::download(const QUrl &url, DownloadSuccess onSuccess, Failure onFailure) {
	QNetworkRequest request{QUrl(m_baseUrl).resolved(url)};
	request.setTransferTimeout(kRequestTimeoutMs);
	if (!m_accessToken.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
	}

	const QByteArray requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
	const QString endpoint = request.url().path();
	EV_LOG_DEBUG(userNetwork, this) << "Downloading content; request_id=" << requestId << "endpoint=" << endpoint;
	QNetworkReply *reply = m_manager->get(request);
	connect(reply, &QNetworkReply::finished, this,
			[this, reply, requestId, endpoint, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)] {
				// 延迟释放保证本轮 finished 回调仍可安全读取响应属性和字节。
				reply->deleteLater();

				const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				if (status == 0) {
					EV_LOG_WARNING(userNetwork, this) << "Transport failure; request_id=" << requestId << "endpoint=" << endpoint << "network_error=" << int(reply->error());
					ApiError error;
					error.message = reply->errorString();
					onFailure(error);
					return;
				}

				const QByteArray payload = reply->readAll();
				if (reply->error() == QNetworkReply::NoError) {
					EV_LOG_DEBUG(userNetwork, this) << "Request completed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status;
					onSuccess(payload);
					return;
				}

				ApiError error;
				error.httpStatus = status;
				EV_LOG_WARNING(userNetwork, this) << "HTTP request failed; request_id=" << requestId << "endpoint=" << endpoint << "http_status=" << status << "network_error=" << int(reply->error());
				error.message = reply->errorString();
				onFailure(error);
			});
}
