#pragma once

#include <QMap>
#include <QNetworkRequest>
#include <QUrl>

namespace Backend {
	/// @brief 由原始参数构造腾讯 GET 请求；可选 SK 按官方规则计算签名，随后编码 URL。
	QNetworkRequest tencentMapRequest(QUrl url, const QMap<QString, QString> &parameters, const QString &secretKey);
	/// @brief 构造供 WebEngine 打开的腾讯路线链接，坐标顺序为纬度、经度。
	QString tencentRouteMapUrl(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode);
} // namespace Backend
