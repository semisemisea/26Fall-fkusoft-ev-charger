#include "api_support.h"

#include "backend/database.h"
#include "evcharger/validation.h"

#include <QDate>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>

#include <optional>

namespace Backend {
	namespace {

		std::optional<Principal> requireDashboardAdmin(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("admin") || principal->status != QStringLiteral("active") || (principal->role != QStringLiteral("ADMIN") && principal->role != QStringLiteral("ADMIN_READONLY"))) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问管理统计"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		struct TimeRange {
			std::optional<QDateTime> from;
			std::optional<QDateTime> to;
		};

		std::optional<TimeRange> parseTimeRange(const HttpRequest &request, bool required, HttpResponse *failure) {
			const bool hasFrom = request.query.hasQueryItem(QStringLiteral("from"));
			const bool hasTo = request.query.hasQueryItem(QStringLiteral("to"));
			if (hasFrom != hasTo || (required && !hasFrom)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("时间区间参数无效"), {}, request.requestId, 400);
				return std::nullopt;
			}
			TimeRange range;
			if (!hasFrom) {
				return range;
			}
			range.from = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("from"), QUrl::FullyDecoded));
			range.to = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("to"), QUrl::FullyDecoded));
			if (!range.from.has_value() || !range.to.has_value() || *range.from >= *range.to) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("时间区间参数无效"), {}, request.requestId, 400);
				return std::nullopt;
			}
			return range;
		}

		std::optional<qint64> parseStationId(const HttpRequest &request, HttpResponse *failure) {
			if (!request.query.hasQueryItem(QStringLiteral("stationId"))) {
				return qint64(0);
			}
			bool ok = false;
			const qint64 stationId = request.query.queryItemValue(QStringLiteral("stationId")).toLongLong(&ok);
			if (!ok || stationId <= 0) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电站筛选参数无效"), QJsonObject{{QStringLiteral("stationId"), QStringLiteral("必须是正整数")}}, request.requestId, 400);
				return std::nullopt;
			}
			return stationId;
		}

		std::optional<int> parseUtcOffset(const HttpRequest &request, HttpResponse *failure) {
			static const QRegularExpression expression(QStringLiteral("^([+-])(\\d{2}):(\\d{2})$"));
			const QString raw = request.query.queryItemValue(QStringLiteral("utcOffset"), QUrl::FullyDecoded);
			const QRegularExpressionMatch match = expression.match(raw);
			if (!match.hasMatch()) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("UTC 偏移无效"), QJsonObject{{QStringLiteral("utcOffset"), QStringLiteral("格式必须为 +HH:MM 或 -HH:MM")}}, request.requestId, 400);
				return std::nullopt;
			}
			const int hours = match.captured(2).toInt();
			const int minutes = match.captured(3).toInt();
			if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("UTC 偏移无效"), QJsonObject{{QStringLiteral("utcOffset"), QStringLiteral("必须在 -14:00 至 +14:00 范围内")}}, request.requestId, 400);
				return std::nullopt;
			}
			const int sign = match.captured(1) == QStringLiteral("-") ? -1 : 1;
			return sign * (hours * 3600 + minutes * 60);
		}

		HttpResponse revenue(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireDashboardAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto range = parseTimeRange(request, false, &failure);
			const auto stationId = parseStationId(request, &failure);
			if (!range.has_value() || !stationId.has_value()) {
				return failure;
			}

			qint64 revenueFen = 0;
			qint64 orderCount = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT COALESCE(SUM(amount_fen),0),COUNT(*) FROM orders WHERE status='settled'");
				if (range->from.has_value()) {
					sql += QStringLiteral(" AND settled_at>=? AND settled_at<?");
				}
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				if (range->from.has_value()) {
					query.addBindValue(toDatabaseTimestamp(*range->from));
					query.addBindValue(toDatabaseTimestamp(*range->to));
				}
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec() || !query.next()) {
					*operationError = query.lastError().text();
					return false;
				}
				revenueFen = query.value(0).toLongLong();
				orderCount = query.value(1).toLongLong();
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("from"), range->from.has_value() ? QJsonValue(toDatabaseTimestamp(*range->from)) : QJsonValue(QJsonValue::Null)},
								{QStringLiteral("to"), range->to.has_value() ? QJsonValue(toDatabaseTimestamp(*range->to)) : QJsonValue(QJsonValue::Null)},
								{QStringLiteral("revenueFen"), revenueFen},
								{QStringLiteral("orderCount"), orderCount},
							},
							request.requestId);
		}

		HttpResponse revenueSeries(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireDashboardAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto range = parseTimeRange(request, true, &failure);
			const auto offsetSeconds = parseUtcOffset(request, &failure);
			const auto stationId = parseStationId(request, &failure);
			if (!range.has_value() || !offsetSeconds.has_value() || !stationId.has_value()) {
				return failure;
			}

			const QDate firstDate = range->from->toOffsetFromUtc(*offsetSeconds).date();
			const QDate lastDate = range->to->addMSecs(-1).toOffsetFromUtc(*offsetSeconds).date();
			QMap<QDate, QPair<qint64, qint64>> totals;
			for (QDate date = firstDate; date.isValid() && date <= lastDate; date = date.addDays(1)) {
				totals.insert(date, {0, 0});
			}

			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT settled_at,amount_fen FROM orders WHERE status='settled' AND settled_at>=? AND settled_at<?");
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				query.addBindValue(toDatabaseTimestamp(*range->from));
				query.addBindValue(toDatabaseTimestamp(*range->to));
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					const QDateTime settledAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
					auto &bucket = totals[settledAt.toOffsetFromUtc(*offsetSeconds).date()];
					bucket.first += query.value(1).toLongLong();
					bucket.second += 1;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}

			QJsonArray points;
			for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
				const QDateTime bucketStart(it.key(), QTime(0, 0), Qt::OffsetFromUTC, *offsetSeconds);
				points.append(QJsonObject{
					{QStringLiteral("localDate"), it.key().toString(Qt::ISODate)},
					{QStringLiteral("bucketStart"), toDatabaseTimestamp(bucketStart)},
					{QStringLiteral("bucketEnd"), toDatabaseTimestamp(bucketStart.addDays(1))},
					{QStringLiteral("revenueFen"), it.value().first},
					{QStringLiteral("orderCount"), it.value().second},
				});
			}
			return jsonData(QJsonObject{
								{QStringLiteral("from"), toDatabaseTimestamp(*range->from)},
								{QStringLiteral("to"), toDatabaseTimestamp(*range->to)},
								{QStringLiteral("utcOffset"), request.query.queryItemValue(QStringLiteral("utcOffset"), QUrl::FullyDecoded)},
								{QStringLiteral("points"), points},
							},
							request.requestId);
		}

		HttpResponse chargerStatus(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireDashboardAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto stationId = parseStationId(request, &failure);
			if (!stationId.has_value()) {
				return failure;
			}

			QList<qint64> counts;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QString sql = QStringLiteral("SELECT COUNT(*),COALESCE(SUM(occupancy_status='available'),0),COALESCE(SUM(occupancy_status='reserved'),0),COALESCE(SUM(occupancy_status='charging'),0),COALESCE(SUM(operational_status='online'),0),COALESCE(SUM(operational_status='fault'),0),COALESCE(SUM(operational_status='offline'),0) FROM chargers WHERE deleted_at IS NULL");
				if (*stationId != 0) {
					sql += QStringLiteral(" AND station_id=?");
				}
				QSqlQuery query(database);
				query.prepare(sql);
				if (*stationId != 0) {
					query.addBindValue(*stationId);
				}
				if (!query.exec() || !query.next()) {
					*operationError = query.lastError().text();
					return false;
				}
				for (int column = 0; column < 7; ++column) {
					counts.append(query.value(column).toLongLong());
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			return jsonData(QJsonObject{
								{QStringLiteral("total"), counts.at(0)},
								{QStringLiteral("occupancy"), QJsonObject{{QStringLiteral("available"), counts.at(1)}, {QStringLiteral("reserved"), counts.at(2)}, {QStringLiteral("charging"), counts.at(3)}}},
								{QStringLiteral("operational"), QJsonObject{{QStringLiteral("online"), counts.at(4)}, {QStringLiteral("fault"), counts.at(5)}, {QStringLiteral("offline"), counts.at(6)}}},
							},
							request.requestId);
		}

	} // namespace

	void registerAdminRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue"), [dependencies](const HttpRequest &request) { return revenue(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/revenue-series"), [dependencies](const HttpRequest &request) { return revenueSeries(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/dashboard/charger-status"), [dependencies](const HttpRequest &request) { return chargerStatus(request, dependencies); });
	}

} // namespace Backend
