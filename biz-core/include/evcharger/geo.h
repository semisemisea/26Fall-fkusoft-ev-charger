/**
 * @file geo.h
 * @brief 球面距离和两位小数舍入工具。
 */

#ifndef EVCHARGER_GEO_H
#define EVCHARGER_GEO_H

namespace EvCharger {

	/**
	 * @brief 使用 Haversine 公式计算两组经纬度之间的球面距离。
	 * @param latitudeA 纬度，单位度。
	 * @param longitudeA 经度，单位度。
	 * @param latitudeB 纬度，单位度。
	 * @param longitudeB 经度，单位度。
	 * @return 球面距离，单位千米。
	 */
	double haversineDistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB);
	/**
	 * @brief 将浮点数舍入到两位小数。
	 * @param value 待校验、舍入或转换的输入值。
	 * @return 按 std::round 规则保留两位小数的值；恰好居中时向远离零方向舍入。
	 */
	double roundToHundredths(double value);

} // namespace EvCharger

#endif
