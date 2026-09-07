#include "evcharger/charging.h"

#include <limits>

namespace EvCharger {
	namespace {

		std::optional<qint64> checkedMultiply(qint64 left, qint64 right) {
			if (left < 0 || right < 0 || (left != 0 && right > std::numeric_limits<qint64>::max() / left)) {
				return std::nullopt;
			}
			return left * right;
		}

		std::optional<qint64> roundHalfUpRatio(qint64 value, qint64 multiplier, qint64 divisor) {
			const auto product = checkedMultiply(value, multiplier);
			if (!product.has_value() || *product > std::numeric_limits<qint64>::max() - divisor / 2) {
				return std::nullopt;
			}
			return (*product + divisor / 2) / divisor;
		}

	} // namespace

	std::optional<ChargeMetrics> calculateCharge(qint64 powerW,
												 qint64 unitPriceFenPerKwh,
												 const QDateTime &startedAt,
												 const QDateTime &calculatedAt) {
		if (powerW <= 0 || unitPriceFenPerKwh <= 0 || !startedAt.isValid() || !calculatedAt.isValid()) {
			return std::nullopt;
		}
		const qint64 seconds = startedAt.secsTo(calculatedAt);
		if (seconds < 0) {
			return std::nullopt;
		}
		const auto energyWs = checkedMultiply(powerW, seconds);
		if (!energyWs.has_value()) {
			return std::nullopt;
		}
		const auto amountFen = roundHalfUpRatio(*energyWs, unitPriceFenPerKwh, 3'600'000);
		const auto energyHundredths = roundHalfUpRatio(*energyWs, 100, 3'600'000);
		if (!amountFen.has_value() || !energyHundredths.has_value()) {
			return std::nullopt;
		}
		return ChargeMetrics{seconds, *energyWs, *energyHundredths, *amountFen};
	}

} // namespace EvCharger
