#ifndef EVCHARGER_CHARGING_H
#define EVCHARGER_CHARGING_H

#include <QDateTime>
#include <QtGlobal>

#include <optional>

namespace EvCharger {

	struct ChargeMetrics {
		qint64 durationSeconds = 0;
		qint64 energyWs = 0;
		qint64 energyHundredthsKwh = 0;
		qint64 amountFen = 0;
	};

	std::optional<ChargeMetrics> calculateCharge(qint64 powerW,
												 qint64 unitPriceFenPerKwh,
												 const QDateTime &startedAt,
												 const QDateTime &calculatedAt);

} // namespace EvCharger

#endif
