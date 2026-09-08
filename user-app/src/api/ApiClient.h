#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QHttpMultiPart;
class QJsonObject;
class QNetworkAccessManager;
class QUrl;

// 服务端统一错误结构：页面按 code 做决策，不解析 message 文字
struct ApiError {
	QString code = QStringLiteral("NETWORK_ERROR"); // 契约错误码
	QString message; // 人类可读信息（界面提示用）
	int httpStatus = 0; // http状态码
	QString requestId; // 服务端追踪 ID，报 bug 时用
};

// 唯一网络出口：统一注入 Bearer 令牌与 X-Request-Id，拆 data/meta 信封，失败回调 ApiError
class ApiClient : public QObject
{
    Q_OBJECT

public:
    using Success = std::function<void(const QJsonValue &data, const QJsonObject &meta)>;
    using Failure = std::function<void(const ApiError &error)>;
    using DownloadSuccess = std::function<void(const QByteArray &payload)>;

    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);

    // 设置/读取访问令牌与服务地址；令牌非空时自动注入 Authorization 头
    void setAccessToken(const QString &token);
    [[nodiscard]] const QString &accessToken() const { return m_accessToken; }
    [[nodiscard]] const QString &baseUrl() const { return m_baseUrl; }

    // GET 请求，path 相对 baseUrl
    void get(const QString &path, Success onSuccess, Failure onFailure);
    // POST JSON 请求
    void post(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
    // PATCH JSON 请求
    void patch(const QString &path, const QJsonObject &body, Success onSuccess, Failure onFailure);
    // multipart 上传（如头像），multiPart 由客户端接管生命周期
    void upload(const QString &path, QHttpMultiPart *multiPart, Success onSuccess, Failure onFailure);
    // 下载文件（如充电账单 PDF），url 可为相对地址
    void download(const QUrl &url, DownloadSuccess onSuccess, Failure onFailure);

private:
    enum class Verb { Get, Post, Patch };

    // get/post/patch 共用的发送实现
    void send(const QString &path, Verb verb, const QJsonObject *body, Success onSuccess, Failure onFailure);

    QString m_baseUrl;
    QString m_accessToken;
    QNetworkAccessManager *m_manager = nullptr;
};
