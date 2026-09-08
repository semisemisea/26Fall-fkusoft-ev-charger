#pragma once

#include <QMap>
#include <QNetworkRequest>
#include <QUrl>

namespace Backend {
	/// @brief 由原始参数构造腾讯 GET 请求；可选 SK 按官方规则计算签名，随后编码 URL。
	QNetworkRequest tencentMapRequest(QUrl url, const QMap<QString, QString> &parameters, const QString &secretKey);
} // namespace Backend
