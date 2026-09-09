/**
 * @file station.h
 * @brief 解析电站位置、计价与电桩统计，保留服务端计算的距离。
 */
#pragma once

#include <QString>

class QJsonObject;

/**
 * @brief 电站：位置、单价与电桩统计（距离由服务端球面计算，客户端不自行推断）
 */
struct Station {
	int id = 0;					   ///< 服务端分配的实体标识；默认 0 表示尚未填充。
	QString name;				   ///< 电站名称。
	QString address;			   ///< 电站文字地址。
	double latitude = 0;		   ///< 电站纬度，单位为度。
	double longitude = 0;		   ///< 电站经度，单位为度。
	qlonglong pricePerKwhFen = 0;  ///< 每千瓦时电价，单位为分；来源 priceFenPerKwh。
	int chargerCount = 0;		   ///< 站内电桩总数。
	int availableChargerCount = 0; ///< 站内当前空闲电桩数。
	double onlineRate = 0;		   ///< 服务端返回的站内在线率数值。
	double distanceKm = 0;		   ///< 服务端计算的当前位置到电站距离，单位 km。
	QString status;				   ///< 站点启用状态字符串，例如 active 或 inactive。

	/**
	 * @brief 从服务端 JSON 对象构造 Station
	 * @param object 服务端 JSON 对象；缺失字段采用 Qt 转换的默认值。
	 * @return 按接口字段映射后的值对象，不进行业务合法性校验。
	 */
	static Station fromJson(const QJsonObject &object);
};
