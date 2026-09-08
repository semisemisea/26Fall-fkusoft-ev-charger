/**
 * @file charging.cpp
 * @brief 整数充电计量与金额四舍五入；功率使用瓦，金额使用分。
 */

#include "evcharger/charging.h"

#include <limits>

namespace EvCharger {
	namespace {

		/**
		 * @brief 计算非负整数乘积，在乘法执行前拒绝负数或 qint64 溢出。
		 * @param left 非负左操作数。
		 * @param right 非负右操作数。
		 * @return 非负乘积；存在负操作数或乘积超出 qint64 时返回 std::nullopt。
		 */
		std::optional<qint64> checkedMultiply(qint64 left, qint64 right) {
			if (left < 0 || right < 0 || (left != 0 && right > std::numeric_limits<qint64>::max() / left)) {
				return std::nullopt;
			}
			return left * right;
		}

		/**
		 * @brief 计算 value × multiplier / divisor，并以整数加半除数实现四舍五入。
		 * @param value 待校验、舍入或转换的输入值。
		 * @param multiplier 非负比例分子。
		 * @param divisor 正比例分母。
		 * @return 按比例四舍五入后的整数；乘法或加半除数溢出时返回 std::nullopt。
		 * @pre divisor 必须大于零；调用方使用固定的正单位换算常量。
		 */
		std::optional<qint64> roundHalfUpRatio(qint64 value, qint64 multiplier, qint64 divisor) {
			const auto product = checkedMultiply(value, multiplier);
			if (!product.has_value() || *product > std::numeric_limits<qint64>::max() - divisor / 2) {
				return std::nullopt;
			}
			return (*product + divisor / 2) / divisor;
		}

	} // namespace

	/**
	 * @brief 按恒定功率与单价计算整秒时长、瓦秒电量及应付分数；电量和金额各自按 half-up 舍入。
	 * @param powerW 恒定充电功率，单位瓦，必须大于零。
	 * @param unitPriceFenPerKwh 每千瓦时价格，单位分，必须大于零。
	 * @param startedAt 有效的充电开始时刻。
	 * @param calculatedAt 有效的计量截止时刻，不得早于开始时刻。
	 * @return 完整计量结果；功率/价格非正、时间无效/倒退或任何中间运算溢出时返回 std::nullopt。
	 */
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
		// 1 kWh = 3,600,000 Ws；费用从原始瓦秒计算，不能先将展示电量舍入。
		const auto amountFen = roundHalfUpRatio(*energyWs, unitPriceFenPerKwh, 3'600'000);
		const auto energyHundredths = roundHalfUpRatio(*energyWs, 100, 3'600'000);
		if (!amountFen.has_value() || !energyHundredths.has_value()) {
			return std::nullopt;
		}
		return ChargeMetrics{seconds, *energyWs, *energyHundredths, *amountFen};
	}

} // namespace EvCharger
