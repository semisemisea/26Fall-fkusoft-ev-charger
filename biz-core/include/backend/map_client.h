#ifndef BACKEND_MAP_CLIENT_H
#define BACKEND_MAP_CLIENT_H

#include <QJsonArray>
#include <QString>

namespace Backend {

	enum class MapStatus {
		Success,
		NotFound,
		ProviderError,
		Unavailable,
	};

	struct GeocodeResult {
		MapStatus status = MapStatus::Unavailable;
		QString address;
		QString formattedAddress;
		double latitude = 0;
		double longitude = 0;
	};

	struct RouteResult {
		MapStatus status = MapStatus::Unavailable;
		qint64 distanceM = 0;
		qint64 durationSec = 0;
		QJsonArray polyline;
		QString mapUrl;
	};

	class MapClient {
	public:
		virtual ~MapClient() = default;

		virtual GeocodeResult geocode(const QString &address, const QString &region) = 0;
		virtual RouteResult route(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) = 0;
	};

	class TencentMapClient final : public MapClient {
	public:
		TencentMapClient(QString key, int timeoutMs, int retryCount);

		GeocodeResult geocode(const QString &address, const QString &region) override;
		RouteResult route(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) override;

	private:
		QString m_key;
		int m_timeoutMs;
		int m_retryCount;
	};

} // namespace Backend

#endif
