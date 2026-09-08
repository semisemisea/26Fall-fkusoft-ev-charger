/**
 * @file reservation.h
 * @brief 解析预约及服务端到期时间，供预约记录和倒计时界面使用。
 */
#pragma once

#include <QDateTime>
#include <QString>

class QJsonObject;

/**
 * @brief 预约：锁定电桩至 expiresAt（默认 15 分钟），超时或取消由服务端释放
 */
struct Reservation {
	int id = 0;			 ///< 服务端分配的实体标识；默认 0 表示尚未填充。
	int chargerId = 0;	 ///< 关联电桩的服务端标识。
	int stationId = 0;	 ///< 所属电站的服务端标识。
	int userId = 0;		 ///< 所属用户的服务端标识。
	QString status;		 ///< 预约阶段：active、used、cancelled 或 expired。
	QString startAt;	 ///< 预约创建时间；来自接口 createdAt 字段。
	QDateTime expiresAt; ///< 按 ISO 日期解析的服务端到期时刻，用于倒计时。
	QString stationName; ///< 按 stationId 生成的站点显示名称。
	QString chargerCode; ///< 按 chargerId 生成的电桩编号文本。

	/**
	 * @brief 从服务端 JSON 对象构造 Reservation
	 * @param object 服务端 JSON 对象；缺失字段采用 Qt 转换的默认值。
	 * @return 按接口字段映射后的值对象，不进行业务合法性校验。
	 */
	static Reservation fromJson(const QJsonObject &object);
	/**
	 * @brief 预约状态的中文标签
	 * @param status 接口返回的状态字符串。
	 * @return 已知状态的中文标签；未知值返回未知状态。
	 */
	static QString statusLabel(const QString &status);
};
