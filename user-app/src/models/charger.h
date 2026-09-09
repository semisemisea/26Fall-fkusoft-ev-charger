/**
 * @file charger.h
 * @brief 将电桩接口的运行状态和占用状态合并为界面展示模型。
 */
#pragma once

#include <QString>

class QJsonObject;

/**
 * @brief 电桩：状态枚举 available/reserved/charging/fault/offline，见 docs/apis.md
 */
struct Charger {
	int id = 0;					///< 服务端分配的实体标识；默认 0 表示尚未填充。
	int stationId = 0;			///< 所属电站的服务端标识。
	QString code;				///< 界面电桩编号；当前由数值 id 转换成文本。
	QString type;				///< 电桩类型字符串，已知值 fast 或 slow。
	double powerKw = 0;			///< 电桩额定功率，单位 kW。
	QString status;				///< online 电桩使用 occupancyStatus，非 online 时优先使用 operationalStatus。
	int totalChargeCount = 0;	///< 电桩累计充电次数。
	int totalChargeMinutes = 0; ///< 电桩累计充电时长，单位分钟。

	/**
	 * @brief 从服务端 JSON 对象构造 Charger
	 * @param object 服务端 JSON 对象；缺失字段采用 Qt 转换的默认值。
	 * @return 按接口字段映射后的值对象，不进行业务合法性校验。
	 */
	static Charger fromJson(const QJsonObject &object);
	/**
	 * @brief 类型（fast/slow）的中文标签
	 * @param charger 目标电桩的界面模型。
	 * @return 快充、慢充或未知类型的中文标签。
	 */
	static QString typeLabel(const Charger &charger);
	/**
	 * @brief 状态的中文标签
	 * @param status 接口返回的状态字符串。
	 * @return 已知状态的中文标签；未知值返回未知状态。
	 */
	static QString statusLabel(const QString &status);
	/**
	 * @brief 状态徽章文字颜色（十六进制色值）
	 * @param status 接口返回的状态字符串。
	 * @return 徽章文字颜色的十六进制字符串。
	 */
	static QString statusColor(const QString &status);
	/**
	 * @brief 状态徽章背景颜色（十六进制色值）
	 * @param status 接口返回的状态字符串。
	 * @return 徽章背景颜色的十六进制字符串。
	 */
	static QString statusBgColor(const QString &status);
};
