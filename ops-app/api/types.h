#pragma once

// 管理端使用的领域类型与 JSON 解析辅助。
// 字段命名与 docs/apis.md 的响应保持一致(snakeCase 已转为 camelCase)。

#include <QJsonObject>
#include <QList>
#include <QString>
#include <qglobal.h>

namespace ops {

	// ---- 解析辅助:字段缺失时给默认值,不崩溃(apis.md 要求忽略未知字段) ----

	inline QString jsonStr(const QJsonObject &o, const char *key, const QString &fallback = {}) {
		const auto v = o.value(QLatin1String(key));
		return v.isString() ? v.toString() : fallback;
	}

	inline qint64 jsonI64(const QJsonObject &o, const char *key, qint64 fallback = 0) {
		const auto v = o.value(QLatin1String(key));
		return v.isDouble() ? static_cast<qint64>(v.toDouble()) : fallback;
	}

	inline double jsonDbl(const QJsonObject &o, const char *key, double fallback = 0.0) {
		const auto v = o.value(QLatin1String(key));
		return v.isDouble() ? v.toDouble() : fallback;
	}

	// 列表分页元数据(apis.md: 列表响应在 meta 中带 page/pageSize/total/hasNext)
	struct PageMeta {
		bool valid = false; // 服务端是否返回分页 meta(false 表示未返回,界面退化为单页展示)
		int page = 1;
		int pageSize = 20;
		qint64 total = 0;
		bool hasNext = false;
	};

	// ---- 值类型 ----

	struct AdminUser {
		qint64 id = 0;
		QString username;
		QString displayName;
		QString role; // ADMIN / ADMIN_READONLY
		QString status;
	};

	struct DashboardSummary {
		QString asOf;
		qint64 todayRevenueFen = 0;
		qint64 monthRevenueFen = 0;
		qint64 totalRevenueFen = 0;
		qint64 userCount = 0;
		qint64 stationCount = 0;
		qint64 chargerCount = 0;
		double onlineRate = 0.0;
	};

	struct RevenuePoint {
		QString bucketStart;
		qint64 revenueFen = 0;
		qint64 orderCount = 0;
	};

	struct ChargerStatusCount {
		QString status; // available/reserved/charging/fault/offline
		qint64 count = 0;
		double percent = 0.0;
	};

	struct Charger {
		qint64 id = 0;
		qint64 stationId = 0;
		QString code;
		QString type; // fast / slow
		double powerKw = 0.0;
		QString status;
		qint64 totalChargeCount = 0;
		qint64 totalChargeMinutes = 0;
	};

	struct StationSummary {
		qint64 id = 0;
		QString name;
		QString address;
		double latitude = 0.0;
		double longitude = 0.0;
		qint64 pricePerKwhFen = 0;
		qint64 chargerCount = 0;
		qint64 availableChargerCount = 0;
		double onlineRate = 0.0;
		QString status;
	};

	struct AdminUserRow {
		qint64 id = 0;
		QString phone;
		QString nickname;
		qint64 walletBalanceFen = 0;
		QString status; // active / frozen
		QString createdAt;
	};

	// 新增电站表单(界面收集数量,由客户端生成电桩清单)
	struct StationForm {
		QString name;
		QString address;
		double latitude = 0.0;
		double longitude = 0.0;
		qint64 pricePerKwhFen = 0;
		qint64 chargerCount = 0;
		qint64 fastCount = 0; // 其中快充数量
		double fastPowerKw = 120.0;
		double slowPowerKw = 7.0;
	};

	// ---- 展示辅助 ----

	inline QString fenCents(qint64 fen) {
		return QString::number(fen / 100.0, 'f', 2);
	}

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
		if (s == QLatin1String("active"))
			return QStringLiteral("正常");
		if (s == QLatin1String("frozen"))
			return QStringLiteral("冻结");
		if (s == QLatin1String("inactive"))
			return QStringLiteral("已下线");
		return QStringLiteral("未知状态"); // apis.md: 枚举新增值时显示通用文案
	}

	inline QString chargerTypeText(const QString &t) {
		return t == QLatin1String("fast") ? QStringLiteral("快充") : QStringLiteral("慢充");
	}

} // namespace ops
