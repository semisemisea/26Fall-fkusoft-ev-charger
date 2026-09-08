/**
 * @file Order.h
 * @brief 解析订单计量、时间和服务端账单金额，提供中文状态标签。
 */
#pragma once

#include <QString>
#include <QtGlobal>

class QJsonObject;

/**
 * @brief 充电订单：账单金额由服务端算定（电量 × 单价），客户端不覆盖
 */
struct Order {
	int id = 0;						  ///< 服务端分配的实体标识；默认 0 表示尚未填充。
	QString orderNo;				  ///< 界面订单编号；当前由数值 id 转换成文本。
	int userId = 0;					  ///< 所属用户的服务端标识。
	int stationId = 0;				  ///< 所属电站的服务端标识。
	int chargerId = 0;				  ///< 关联电桩的服务端标识。
	QString status;					  ///< 订单阶段：charging、awaiting_payment、settled、cancelled 或 failed。
	QString startedAt;				  ///< 充电开始时间的原始 ISO 文本。
	QString endedAt;				  ///< 停止充电时间；来自接口 stoppedAt 字段。
	double energyKwh = 0;			  ///< 服务端累计充入电量，单位 kWh，不设界面容量上限。
	int durationMinutes = 0;		  ///< 服务端累计充电时长，单位分钟。
	qlonglong unitPriceFenPerKwh = 0; ///< 订单单价快照，单位分/kWh。
	qlonglong amountFen = 0;		  ///< 服务端计算的订单金额，单位为分。
	QString stationName;			  ///< 按 stationId 生成的站点显示名称。
	QString chargerCode;			  ///< 按 chargerId 生成的电桩编号文本。

	/**
	 * @brief 从服务端 JSON 对象构造 Order
	 * @param object 服务端 JSON 对象；缺失字段采用 Qt 转换的默认值。
	 * @return 按接口字段映射后的值对象，不进行业务合法性校验。
	 */
	static Order fromJson(const QJsonObject &object);
	/**
	 * @brief 订单状态的中文标签
	 * @param status 接口返回的状态字符串。
	 * @return 已知状态的中文标签；未知值返回未知状态。
	 */
	static QString statusLabel(const QString &status);
};
