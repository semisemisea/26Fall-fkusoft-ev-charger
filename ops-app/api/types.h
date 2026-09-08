/** @file
 * @brief 管理员界面的传输值类型、容错解析和展示辅助。
 */
#pragma once

// 管理端使用的领域类型与 JSON 解析辅助。
// 字段命名与 docs/apis.md 的响应保持一致(snakeCase 已转为 camelCase)。

#include <QJsonObject>
#include <QList>
#include <QString>
#include <qglobal.h>

namespace ops {

	// ---- 解析辅助:字段缺失时给默认值,不崩溃(apis.md 要求忽略未知字段) ----

	/** @brief 读取字符串字段；缺失或类型不符时使用 fallback。
	 * @param o 待读取的 JSON 对象。
	 * @param key JSON 字段名。
	 * @param fallback 字段不存在或类型不匹配时的默认值。
	 * @return 字符串字段值，类型不符时为 fallback。
	 */
	inline QString jsonStr(const QJsonObject &o, const char *key, const QString &fallback = {}) {
		const auto v = o.value(QLatin1String(key));
		return v.isString() ? v.toString() : fallback;
	}

	/** @brief 读取 JSON 数字并转换为整数；非数字使用 fallback。
	 * @param o 待读取的 JSON 对象。
	 * @param key JSON 字段名。
	 * @param fallback 字段不存在或类型不匹配时的默认值。
	 * @return 数字转换得到的 qint64，非数字时为 fallback。
	 */
	inline qint64 jsonI64(const QJsonObject &o, const char *key, qint64 fallback = 0) {
		const auto v = o.value(QLatin1String(key));
		return v.isDouble() ? static_cast<qint64>(v.toDouble()) : fallback;
	}

	/** @brief 读取浮点字段；缺失或类型不符时使用 fallback。
	 * @param o 待读取的 JSON 对象。
	 * @param key JSON 字段名。
	 * @param fallback 字段不存在或类型不匹配时的默认值。
	 * @return 数字字段值，类型不符时为 fallback。
	 */
	inline double jsonDbl(const QJsonObject &o, const char *key, double fallback = 0.0) {
		const auto v = o.value(QLatin1String(key));
		return v.isDouble() ? v.toDouble() : fallback;
	}

	// 列表分页元数据(apis.md: 列表响应在 meta 中带 page/pageSize/total/hasNext)
	/// @brief 列表分页元数据；缺失时 valid 为 false，界面不能据此继续翻页。
	struct PageMeta {
		bool valid = false;	  ///< 服务端是否提供可用分页元数据。
		int page = 1;		  ///< 从 1 开始的当前页码。
		int pageSize = 20;	  ///< 服务端返回的每页数量。
		qint64 total = 0;	  ///< 当前统计或分页结果的总数量。
		bool hasNext = false; ///< 服务端是否声明存在下一页。
	};

	// ---- 值类型 ----

	/// @brief 登录返回的管理员身份和权限信息。
	struct AdminUser {
		qint64 id = 0;		 ///< 服务端实体 ID。
		QString username;	 ///< 登录账号。
		QString displayName; ///< 管理员展示名。
		QString role;		 ///< 角色标识 ADMIN 或 ADMIN_READONLY。
		QString status;		 ///< 对应实体的服务端状态字符串。
	};

	/// @brief 由多个接口汇总的营收与资源数量快照。
	struct DashboardSummary {
		QString asOf;				///< 汇总请求发起时的 UTC ISO 时间。
		qint64 todayRevenueFen = 0; ///< 今日累计营收，单位分。
		qint64 monthRevenueFen = 0; ///< 本月累计营收，单位分。
		qint64 totalRevenueFen = 0; ///< 全时段累计营收，单位分。
		qint64 userCount = 0;		///< 用户数量；只读汇总不请求此值。
		qint64 stationCount = 0;	///< 电站总数量。
		qint64 chargerCount = 0;	///< 电桩总数量。
		double onlineRate = 0.0;	///< 在线数量除以总数量的比率，零总数时为零。
	};

	/// @brief 按日期分桶的营收和订单计数。
	struct RevenuePoint {
		QString bucketStart;   ///< 营收时间桶的起始 ISO 时间。
		qint64 revenueFen = 0; ///< 时间桶内营收，单位分。
		qint64 orderCount = 0; ///< 时间桶内订单数量。
	};

	/// @brief 一个状态维度中单个状态的数量和占比。
	struct ChargerStatusCount {
		QString status;		  ///< 对应实体的服务端状态字符串。
		qint64 count = 0;	  ///< 当前状态下的电桩数量。
		double percent = 0.0; ///< 占电桩总数的比率，展示时乘以 100。
	};

	/// @brief 占用、运维两个独立维度的电桩统计。
	struct ChargerStatusSnapshot {
		qint64 total = 0;					   ///< 当前统计或分页结果的总数量。
		QList<ChargerStatusCount> occupancy;   ///< available、reserved、charging 的统计行。
		QList<ChargerStatusCount> operational; ///< online、fault、offline 的统计行。
	};

	/// @brief 管理界面中的电桩值对象；占用状态不与运维状态合并存储。
	struct Charger {
		qint64 id = 0;				   ///< 服务端实体 ID。
		qint64 stationId = 0;		   ///< 电桩所属电站 ID。
		QString code;				   ///< 展示编号；JSON 解析时使用 ID 的十进制文本。
		QString type;				   ///< fast 快充或 slow 慢充。
		double powerKw = 0.0;		   ///< 充电功率，单位千瓦。
		QString occupancyStatus;	   ///< 独立占用状态 available/reserved/charging。
		QString operationalStatus;	   ///< 独立运维状态 online/fault/offline。
		qint64 totalChargeCount = 0;   ///< 累计充电次数。
		qint64 totalChargeMinutes = 0; ///< 累计充电时长，单位分钟。
	};

	/// @brief 电桩可编辑字段；创建请求不发送运维状态。
	struct ChargerForm {
		QString type = QStringLiteral("fast"); ///< fast 快充或 slow 慢充。
		double powerKw = 120.0;				   ///< 充电功率，单位千瓦。
		QString operationalStatus;			   ///< 独立运维状态 online/fault/offline。
	};

	/// @brief 电站列表行及客户端补充的在线率。
	struct StationSummary {
		qint64 id = 0;					  ///< 服务端实体 ID。
		QString name;					  ///< 电站名称。
		double latitude = 0.0;			  ///< 纬度，单位度。
		double longitude = 0.0;			  ///< 经度，单位度。
		qint64 pricePerKwhFen = 0;		  ///< 每千瓦时单价，单位分。
		qint64 chargerCount = 0;		  ///< 电桩总数量。
		qint64 availableChargerCount = 0; ///< 服务端返回的可用电桩数量。
		double onlineRate = 0.0;		  ///< 在线数量除以总数量的比率，零总数时为零。
		QString status;					  ///< 对应实体的服务端状态字符串。
	};

	/// @brief 用户管理列表中展示的账户信息。
	struct AdminUserRow {
		qint64 id = 0;				 ///< 服务端实体 ID。
		QString phone;				 ///< 用户手机号。
		QString nickname;			 ///< 用户昵称。
		qint64 walletBalanceFen = 0; ///< 钱包余额，单位分。
		QString status;				 ///< 对应实体的服务端状态字符串。
		QString createdAt;			 ///< 用户注册 ISO 时间。
	};

	// 新增电站表单;电桩通过独立管理操作逐个创建。
	/// @brief 仅创建电站基本信息的表单；电桩通过独立操作添加。
	struct StationForm {
		QString name;			   ///< 电站名称。
		double latitude = 0.0;	   ///< 纬度，单位度。
		double longitude = 0.0;	   ///< 经度，单位度。
		qint64 pricePerKwhFen = 0; ///< 每千瓦时单价，单位分。
	};

	// ---- 展示辅助 ----

	/** @brief 将分转换为固定两位小数的元文本。
	 * @param fen 以分为单位的金额。
	 * @return 固定两位小数的元金额文本。
	 */
	inline QString fenCents(qint64 fen) {
		return QString::number(fen / 100.0, 'f', 2);
	}

	/** @brief 将已知状态翻译为中文，未知值统一显示未知状态。
	 * @param s 服务端状态字符串。
	 * @return 对应中文状态；未知值为“未知状态”。
	 */
	inline QString statusText(const QString &s) {
		if (s == QLatin1String("available"))
			return QStringLiteral("闲置");
		if (s == QLatin1String("reserved"))
			return QStringLiteral("预约");
		if (s == QLatin1String("charging"))
			return QStringLiteral("在用");
		if (s == QLatin1String("fault"))
			return QStringLiteral("故障");
		if (s == QLatin1String("offline"))
			return QStringLiteral("离线");
		if (s == QLatin1String("online"))
			return QStringLiteral("在线");
		if (s == QLatin1String("active"))
			return QStringLiteral("正常");
		if (s == QLatin1String("frozen"))
			return QStringLiteral("冻结");
		if (s == QLatin1String("inactive"))
			return QStringLiteral("已下线");
		return QStringLiteral("未知状态"); // apis.md: 枚举新增值时显示通用文案
	}

	/** @brief 将 fast 显示为快充，其他值显示为慢充。
	 * @param t 电桩类型字符串。
	 * @return fast 对应“快充”，其他值对应“慢充”。
	 */
	inline QString chargerTypeText(const QString &t) {
		return t == QLatin1String("fast") ? QStringLiteral("快充") : QStringLiteral("慢充");
	}

	/** @brief 解析电桩字段，并使用数字 ID 生成展示编号。
	 * @param object 服务返回的电桩 JSON 对象。
	 * @return 解析后的值对象，缺失字段保留解析默认值。
	 */
	inline Charger chargerFromJson(const QJsonObject &object) {
		Charger charger;
		charger.id = jsonI64(object, "id");
		charger.stationId = jsonI64(object, "stationId");
		charger.code = QString::number(charger.id);
		charger.type = jsonStr(object, "type");
		charger.powerKw = jsonDbl(object, "powerKw");
		charger.occupancyStatus = jsonStr(object, "occupancyStatus");
		charger.operationalStatus = jsonStr(object, "operationalStatus");
		charger.totalChargeCount = jsonI64(object, "totalChargeCount");
		charger.totalChargeMinutes = jsonI64(object, "totalChargeMinutes");
		return charger;
	}

	/** @brief 判断是否为闲置且故障或离线的可重启电桩。
	 * @param charger 可选初始电桩；空指针表示新增，构造时复制控件所需值。
	 * @return 符合重启界面条件时为 true。
	 */
	inline bool isRestartable(const Charger &charger) {
		return charger.occupancyStatus == QLatin1String("available") &&
			   (charger.operationalStatus == QLatin1String("fault") ||
				charger.operationalStatus == QLatin1String("offline"));
	}

	/** @brief 在线时仅展示占用状态，其他运维状态追加到占用文案后。
	 * @param charger 可选初始电桩；空指针表示新增，构造时复制控件所需值。
	 * @return 占用文案，非在线状态时附加“ / 运维状态”。
	 */
	inline QString chargerStatusText(const Charger &charger) {
		const QString occupancy = statusText(charger.occupancyStatus); ///< available、reserved、charging 的统计行。
		if (charger.operationalStatus == QLatin1String("online"))
			return occupancy; ///< available、reserved、charging 的统计行。
		return QStringLiteral("%1 / %2").arg(occupancy, statusText(charger.operationalStatus));
	}

} // namespace ops
