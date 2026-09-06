#include "backend/map_client.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <optional>
#include <utility>

namespace Backend {
	namespace {

		struct NetworkResult {
			MapStatus status = MapStatus::Unavailable;
			QByteArray body;
		};

		NetworkResult getWithRetry(const QUrl &url, int timeoutMs, int retryCount) {
			QNetworkAccessManager manager;
			for (int attempt = 0; attempt <= retryCount; ++attempt) {
				QNetworkReply *reply = manager.get(QNetworkRequest(url));
				QEventLoop loop;
				QTimer timer;
				timer.setSingleShot(true);
				bool timedOut = false;
				QObject::connect(&timer, &QTimer::timeout, reply, [&]() {
					timedOut = true;
					reply->abort();
				});
				QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
				timer.start(timeoutMs);
				loop.exec();
				timer.stop();

				const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const QNetworkReply::NetworkError networkError = reply->error();
				const QByteArray body = reply->readAll();
				reply->deleteLater();
				if (httpStatus >= 500 || timedOut || (networkError != QNetworkReply::NoError && httpStatus == 0)) {
					if (attempt < retryCount) {
						continue;
					}
					return {MapStatus::Unavailable, {}};
				}
				if (httpStatus >= 400 || networkError != QNetworkReply::NoError) {
					return {MapStatus::ProviderError, {}};
				}
				return {MapStatus::Success, body};
			}
			return {};
		}

		std::optional<QJsonObject> successfulPayload(const NetworkResult &network) {
			if (network.status != MapStatus::Success) {
				return std::nullopt;
			}
			QJsonParseError error;
			const QJsonDocument document = QJsonDocument::fromJson(network.body, &error);
			if (error.error != QJsonParseError::NoError || !document.isObject()) {
				return std::nullopt;
			}
			const QJsonObject root = document.object();
			if (!root.value(QStringLiteral("status")).isDouble() || root.value(QStringLiteral("status")).toInt(-1) != 0 || !root.value(QStringLiteral("result")).isObject()) {
				return std::nullopt;
			}
			return root.value(QStringLiteral("result")).toObject();
		}

		QString coordinate(double latitude, double longitude) {
			return QStringLiteral("%1,%2").arg(QString::number(latitude, 'f', 6), QString::number(longitude, 'f', 6));
		}

		QString routeMapUrl(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) {
			QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
			QUrlQuery query;
			query.addQueryItem(QStringLiteral("type"), mode == QStringLiteral("driving") ? QStringLiteral("drive") : QStringLiteral("walk"));
			query.addQueryItem(QStringLiteral("fromcoord"), coordinate(fromLatitude, fromLongitude));
			query.addQueryItem(QStringLiteral("tocoord"), coordinate(toLatitude, toLongitude));
			query.addQueryItem(QStringLiteral("policy"), QStringLiteral("0"));
			query.addQueryItem(QStringLiteral("referer"), QStringLiteral("ev-charger"));
			url.setQuery(query);
			return url.toString(QUrl::FullyEncoded);
		}

		std::optional<QJsonArray> decodePolyline(const QJsonArray &encoded) {
			if (encoded.size() < 2 || encoded.size() % 2 != 0) {
				return std::nullopt;
			}
			for (const QJsonValue &value : encoded) {
				if (!value.isDouble() || !std::isfinite(value.toDouble())) {
					return std::nullopt;
				}
			}
			double latitude = encoded.at(0).toDouble();
			double longitude = encoded.at(1).toDouble();
			QJsonArray result;
			result.append(QJsonArray{latitude, longitude});
			for (qsizetype index = 2; index < encoded.size(); index += 2) {
				latitude += encoded.at(index).toDouble() / 1'000'000.0;
				longitude += encoded.at(index + 1).toDouble() / 1'000'000.0;
				result.append(QJsonArray{latitude, longitude});
			}
			return result;
		}

	} // namespace

	TencentMapClient::TencentMapClient(QString key, int timeoutMs, int retryCount)
		: m_key(std::move(key)), m_timeoutMs(timeoutMs), m_retryCount(retryCount) {
	}

	GeocodeResult TencentMapClient::geocode(const QString &address, const QString &region) {
		if (m_key.isEmpty()) {
			return {};
		}
		QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("address"), address);
		if (!region.isEmpty()) {
			query.addQueryItem(QStringLiteral("region"), region);
		}
		query.addQueryItem(QStringLiteral("key"), m_key);
		url.setQuery(query);
		const NetworkResult network = getWithRetry(url, m_timeoutMs, m_retryCount);
		if (network.status != MapStatus::Success) {
			return GeocodeResult{network.status};
		}
		const auto payload = successfulPayload(network);
		if (!payload.has_value()) {
			return GeocodeResult{MapStatus::ProviderError};
		}
		const QJsonObject location = payload->value(QStringLiteral("location")).toObject();
		if (!location.value(QStringLiteral("lat")).isDouble() || !location.value(QStringLiteral("lng")).isDouble()) {
			return GeocodeResult{MapStatus::NotFound};
		}
		const double latitude = location.value(QStringLiteral("lat")).toDouble();
		const double longitude = location.value(QStringLiteral("lng")).toDouble();
		if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
			return GeocodeResult{MapStatus::ProviderError};
		}
		const QString formattedAddress = payload->value(QStringLiteral("title")).toString(address);
		return GeocodeResult{MapStatus::Success, address, formattedAddress, latitude, longitude};
	}

	RouteResult TencentMapClient::route(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) {
		if (m_key.isEmpty()) {
			return {};
		}
		QUrl url(QStringLiteral("https://apis.map.qq.com/ws/direction/v1/%1/").arg(mode));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("from"), coordinate(fromLatitude, fromLongitude));
		query.addQueryItem(QStringLiteral("to"), coordinate(toLatitude, toLongitude));
		query.addQueryItem(QStringLiteral("key"), m_key);
		url.setQuery(query);
		const NetworkResult network = getWithRetry(url, m_timeoutMs, m_retryCount);
		if (network.status != MapStatus::Success) {
			return RouteResult{network.status};
		}
		const auto payload = successfulPayload(network);
		if (!payload.has_value()) {
			return RouteResult{MapStatus::ProviderError};
		}
		const QJsonArray routes = payload->value(QStringLiteral("routes")).toArray();
		if (routes.isEmpty() || !routes.first().isObject()) {
			return RouteResult{MapStatus::NotFound};
		}
		const QJsonObject route = routes.first().toObject();
		const qint64 distance = route.value(QStringLiteral("distance")).toInteger(-1);
		const qint64 duration = route.value(QStringLiteral("duration")).toInteger(-1);
		const auto polyline = decodePolyline(route.value(QStringLiteral("polyline")).toArray());
		if (distance < 0 || duration < 0 || !polyline.has_value()) {
			return RouteResult{MapStatus::ProviderError};
		}
		return RouteResult{MapStatus::Success, distance, duration, *polyline, routeMapUrl(fromLatitude, fromLongitude, toLatitude, toLongitude, mode)};
	}

} // namespace Backend
