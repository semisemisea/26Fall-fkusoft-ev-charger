#include "evcharger/validation.h"

#include <QRegularExpression>

#include <cmath>

namespace EvCharger {

	bool isPhoneNumber(const QString &phone) {
		if (phone.size() != 11) {
			return false;
		}
		for (const QChar character : phone) {
			if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
				return false;
			}
		}
		return true;
	}

	bool hasTrimmedLength(const QString &value, qsizetype minimum, qsizetype maximum, QString *trimmed) {
		const QString normalized = value.trimmed();
		if (trimmed != nullptr) {
			*trimmed = normalized;
		}
		return normalized.size() >= minimum && normalized.size() <= maximum;
	}

	std::optional<qint64> parsePowerWatts(const QJsonValue &value, qint64 maximumWatts) {
		if (!value.isDouble()) {
			return std::nullopt;
		}
		const double powerKw = value.toDouble();
		const double scaled = powerKw * 1000.0;
		const qint64 watts = qRound64(scaled);
		if (!std::isfinite(powerKw) || watts <= 0 || watts > maximumWatts || std::abs(scaled - static_cast<double>(watts)) > 1e-7) {
			return std::nullopt;
		}
		return watts;
	}

	std::optional<QDateTime> parseExplicitRfc3339(const QString &value) {
		static const QRegularExpression explicitZone(QStringLiteral("(?:Z|[+-][0-9]{2}:[0-9]{2})$"), QRegularExpression::CaseInsensitiveOption);
		if (!explicitZone.match(value).hasMatch()) {
			return std::nullopt;
		}
		const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
		const QDateTime fallback = parsed.isValid() ? parsed : QDateTime::fromString(value, Qt::ISODate);
		if (!fallback.isValid()) {
			return std::nullopt;
		}
		return fallback.toUTC();
	}

} // namespace EvCharger
