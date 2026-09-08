#ifndef BACKEND_CONFIG_H
#define BACKEND_CONFIG_H

#include <QProcessEnvironment>
#include <QString>

#include <optional>

namespace Backend {

	struct Config {
		QString host = QStringLiteral("127.0.0.1");
		quint16 port = 8080;
		QString databasePath;
		int databaseBusyTimeoutMs = 5000;
		int maxReservationMinutes = 60;
		double maxRadiusKm = 50.0;
		qint64 maxChargerPowerW = 1'000'000;
		qint64 maxStationPriceFenPerKwh = 1'000'000;
		qint64 topUpMinFen = 100;
		qint64 topUpMaxFen = 10'000'000;
		qint64 maxWalletBalanceFen = 1'000'000'000;
		int idempotencyRetentionHours = 24;
		int mapTimeoutMs = 3000;
		int mapRetryCount = 1;
		qint64 jsonBodyLimitBytes = 1'048'576;
		qint64 avatarBodyLimitBytes = 5'242'880;
		int shutdownTimeoutMs = 1000;
		QString tencentMapKey;
		QString serviceToken;

		static std::optional<Config> load(const QString &configPath,
										  const QString &executableDirectory,
										  const QProcessEnvironment &environment,
										  QString *errorMessage);
	};

} // namespace Backend

#endif
