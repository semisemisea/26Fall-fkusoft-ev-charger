/**
 * @file charging.h
 * @brief 整数充电计量与金额四舍五入；功率使用瓦，金额使用分。
 */

#ifndef EVCHARGER_CHARGING_H
#define EVCHARGER_CHARGING_H

#include <QDateTime>
#include <QtGlobal>

#include <optional>

namespace EvCharger {

	/** @brief 一次恒功率充电的整数计量结果；金额与展示电量分别舍入。 */
	struct ChargeMetrics {
		qint64 durationSeconds = 0;		///< 从开始到截止时刻的整秒时长。
		qint64 energyWs = 0;			///< 未做展示舍入的电量，单位瓦秒。
		qint64 energyHundredthsKwh = 0; ///< 四舍五入后的百分之一千瓦时数，除以 100 得展示电量。
		qint64 amountFen = 0;			///< 按快照单价计算并四舍五入的金额，单位分。
	};

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
												 const QDateTime &calculatedAt);

} // namespace EvCharger

#endif
