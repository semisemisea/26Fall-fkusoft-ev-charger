#ifndef EVCHARGER_VALIDATION_H
#define EVCHARGER_VALIDATION_H

#include <QDateTime>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace EvCharger {

	bool isPhoneNumber(const QString &phone);
	bool hasTrimmedLength(const QString &value, qsizetype minimum, qsizetype maximum, QString *trimmed = nullptr);
	std::optional<qint64> parsePowerWatts(const QJsonValue &value, qint64 maximumWatts);
	std::optional<QDateTime> parseExplicitRfc3339(const QString &value);

} // namespace EvCharger

#endif
