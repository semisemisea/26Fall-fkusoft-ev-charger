#include "backend/config.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QLocale>
#include <QSettings>

#include <cmath>
#include <limits>

namespace Backend {
	namespace {

		QString settingValue(const QSettings &settings,
							 const QProcessEnvironment &environment,
							 const QString &iniKey,
							 const QString &environmentKey,
							 const QString &defaultValue) {
			if (environment.contains(environmentKey)) {
				return environment.value(environmentKey);
			}
			if (settings.contains(iniKey)) {
				return settings.value(iniKey).toString();
			}
			return defaultValue;
		}

		bool parseInteger(const QString &raw, qint64 minimum, qint64 maximum, qint64 *value) {
			bool ok = false;
			const qint64 parsed = raw.toLongLong(&ok, 10);
			if (!ok || parsed < minimum || parsed > maximum) {
				return false;
			}
			*value = parsed;
			return true;
		}

		bool parsePositiveDouble(const QString &raw, double *value) {
			bool ok = false;
			const double parsed = QLocale::c().toDouble(raw, &ok);
			if (!ok || !std::isfinite(parsed) || parsed <= 0.0) {
				return false;
			}
			*value = parsed;
			return true;
		}

		bool parsePowerWatts(const QString &raw, qint64 *value) {
			const QString trimmed = raw.trimmed();
			const qsizetype separator = trimmed.indexOf(QLatin1Char('.'));
			if (separator != trimmed.lastIndexOf(QLatin1Char('.'))) {
				return false;
			}
			QString whole = separator < 0 ? trimmed : trimmed.left(separator);
			QString fraction = separator < 0 ? QString() : trimmed.mid(separator + 1);
			if (whole.isEmpty() || fraction.size() > 3) {
				return false;
			}
			for (const QChar character : whole + fraction) {
				if (!character.isDigit() || character.unicode() > QLatin1Char('9').unicode()) {
					return false;
				}
			}
			fraction = fraction.leftJustified(3, QLatin1Char('0'));
			bool wholeOk = false;
			const qint64 wholeNumber = whole.toLongLong(&wholeOk);
			if (!wholeOk || wholeNumber > (std::numeric_limits<qint64>::max() / 1000)) {
				return false;
			}
			const qint64 watts = wholeNumber * 1000 + (fraction.isEmpty() ? 0 : fraction.toLongLong());
			if (watts <= 0) {
				return false;
			}
			*value = watts;
			return true;
		}

		template <typename T>
		bool assignInteger(const QString &raw, qint64 minimum, qint64 maximum, T *target, const QString &name, QString *errorMessage) {
			qint64 value = 0;
			if (!parseInteger(raw, minimum, maximum, &value)) {
				*errorMessage = QStringLiteral("Invalid value for %1").arg(name);
				return false;
			}
			*target = static_cast<T>(value);
			return true;
		}

	} // namespace

	std::optional<Config> Config::load(const QString &configPath,
									   const QString &executableDirectory,
									   const QProcessEnvironment &environment,
									   QString *errorMessage) {
		QString ignoredError;
		if (errorMessage == nullptr) {
			errorMessage = &ignoredError;
		}
		errorMessage->clear();

		QSettings settings(configPath, QSettings::IniFormat);
		if (settings.status() != QSettings::NoError) {
			*errorMessage = QStringLiteral("Unable to read configuration file");
			return std::nullopt;
		}

		Config config;
		config.host = settingValue(settings, environment, QStringLiteral("server/host"), QStringLiteral("EV_CHARGER_HTTP_HOST"), config.host).trimmed();
		QHostAddress address;
		if (config.host.isEmpty() || !address.setAddress(config.host)) {
			*errorMessage = QStringLiteral("Invalid value for server/host");
			return std::nullopt;
		}

		if (!assignInteger(settingValue(settings, environment, QStringLiteral("server/port"), QStringLiteral("EV_CHARGER_HTTP_PORT"), QString::number(config.port)), 1, 65535, &config.port, QStringLiteral("server/port"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("database/busyTimeoutMs"), QStringLiteral("EV_CHARGER_DATABASE_BUSY_TIMEOUT_MS"), QString::number(config.databaseBusyTimeoutMs)), 1, std::numeric_limits<int>::max(), &config.databaseBusyTimeoutMs, QStringLiteral("database/busyTimeoutMs"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("reservation/maxHoldMinutes"), QStringLiteral("EV_CHARGER_MAX_RESERVATION_MINUTES"), QString::number(config.maxReservationMinutes)), 1, std::numeric_limits<int>::max(), &config.maxReservationMinutes, QStringLiteral("reservation/maxHoldMinutes"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("station/maxPriceFenPerKwh"), QStringLiteral("EV_CHARGER_MAX_PRICE_FEN_PER_KWH"), QString::number(config.maxStationPriceFenPerKwh)), 1, std::numeric_limits<qint64>::max(), &config.maxStationPriceFenPerKwh, QStringLiteral("station/maxPriceFenPerKwh"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("wallet/topUpMinFen"), QStringLiteral("EV_CHARGER_TOP_UP_MIN_FEN"), QString::number(config.topUpMinFen)), 1, std::numeric_limits<qint64>::max(), &config.topUpMinFen, QStringLiteral("wallet/topUpMinFen"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("wallet/topUpMaxFen"), QStringLiteral("EV_CHARGER_TOP_UP_MAX_FEN"), QString::number(config.topUpMaxFen)), 1, std::numeric_limits<qint64>::max(), &config.topUpMaxFen, QStringLiteral("wallet/topUpMaxFen"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("wallet/maxBalanceFen"), QStringLiteral("EV_CHARGER_MAX_BALANCE_FEN"), QString::number(config.maxWalletBalanceFen)), 1, std::numeric_limits<qint64>::max(), &config.maxWalletBalanceFen, QStringLiteral("wallet/maxBalanceFen"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("idempotency/retentionHours"), QStringLiteral("EV_CHARGER_IDEMPOTENCY_RETENTION_HOURS"), QString::number(config.idempotencyRetentionHours)), 1, std::numeric_limits<int>::max(), &config.idempotencyRetentionHours, QStringLiteral("idempotency/retentionHours"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("map/timeoutMs"), QStringLiteral("EV_CHARGER_MAP_TIMEOUT_MS"), QString::number(config.mapTimeoutMs)), 1, std::numeric_limits<int>::max(), &config.mapTimeoutMs, QStringLiteral("map/timeoutMs"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("map/retryCount"), QStringLiteral("EV_CHARGER_MAP_RETRY_COUNT"), QString::number(config.mapRetryCount)), 0, std::numeric_limits<int>::max(), &config.mapRetryCount, QStringLiteral("map/retryCount"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("http/jsonBodyLimitBytes"), QStringLiteral("EV_CHARGER_JSON_BODY_LIMIT_BYTES"), QString::number(config.jsonBodyLimitBytes)), 1, std::numeric_limits<qint64>::max(), &config.jsonBodyLimitBytes, QStringLiteral("http/jsonBodyLimitBytes"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("http/avatarBodyLimitBytes"), QStringLiteral("EV_CHARGER_AVATAR_BODY_LIMIT_BYTES"), QString::number(config.avatarBodyLimitBytes)), 1, std::numeric_limits<qint64>::max(), &config.avatarBodyLimitBytes, QStringLiteral("http/avatarBodyLimitBytes"), errorMessage) || !assignInteger(settingValue(settings, environment, QStringLiteral("server/shutdownTimeoutMs"), QStringLiteral("EV_CHARGER_SHUTDOWN_TIMEOUT_MS"), QString::number(config.shutdownTimeoutMs)), 1, std::numeric_limits<int>::max(), &config.shutdownTimeoutMs, QStringLiteral("server/shutdownTimeoutMs"), errorMessage)) {
			return std::nullopt;
		}

		if (!parsePositiveDouble(settingValue(settings, environment, QStringLiteral("location/maxRadiusKm"), QStringLiteral("EV_CHARGER_MAX_RADIUS_KM"), QString::number(config.maxRadiusKm)), &config.maxRadiusKm)) {
			*errorMessage = QStringLiteral("Invalid value for location/maxRadiusKm");
			return std::nullopt;
		}
		if (!parsePowerWatts(settingValue(settings, environment, QStringLiteral("charger/maxPowerKw"), QStringLiteral("EV_CHARGER_MAX_POWER_KW"), QString::number(config.maxChargerPowerW / 1000)), &config.maxChargerPowerW)) {
			*errorMessage = QStringLiteral("Invalid value for charger/maxPowerKw");
			return std::nullopt;
		}
		if (config.topUpMinFen > config.topUpMaxFen || config.maxWalletBalanceFen < config.topUpMinFen) {
			*errorMessage = QStringLiteral("Wallet configuration limits are inconsistent");
			return std::nullopt;
		}

		QString databasePath = settingValue(settings, environment, QStringLiteral("database/path"), QStringLiteral("EV_CHARGER_DATABASE_PATH"), QStringLiteral("data/ev-charger.sqlite3")).trimmed();
		if (databasePath.isEmpty()) {
			*errorMessage = QStringLiteral("Invalid value for database/path");
			return std::nullopt;
		}
		if (QFileInfo(databasePath).isRelative()) {
			databasePath = QDir(executableDirectory).absoluteFilePath(databasePath);
		}
		config.databasePath = QDir::cleanPath(databasePath);
		config.tencentMapKey = environment.value(QStringLiteral("TENCENT_MAP_KEY"));
		config.serviceToken = environment.value(QStringLiteral("ML_SERVICE_TOKEN"));
		return config;
	}

} // namespace Backend
