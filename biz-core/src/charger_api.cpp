#include "api_support.h"

#include "resource_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"
#include "evcharger/validation.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <optional>

namespace Backend {
	namespace {

		struct Pagination {
			int page = 1;
			int pageSize = 20;
		};

		std::optional<qint64> positiveId(const QString &raw) {
			bool ok = false;
			const qint64 id = raw.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		std::optional<Principal> requireAdmin(const HttpRequest &request, const ApiDependencies &dependencies, bool write, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("admin") || principal->status != QStringLiteral("active") || (write ? principal->role != QStringLiteral("ADMIN") : (principal->role != QStringLiteral("ADMIN") && principal->role != QStringLiteral("ADMIN_READONLY")))) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权执行此操作"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		std::optional<Pagination> parsePagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination result;
			auto parse = [&](const QString &name, int fallback, int maximum) -> std::optional<int> {
				if (!request.query.hasQueryItem(name)) {
					return fallback;
				}
				bool ok = false;
				const int value = request.query.queryItemValue(name).toInt(&ok);
				if (!ok || value < 1 || value > maximum) {
					*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("分页参数无效"), QJsonObject{{name, QStringLiteral("超出合法范围")}}, request.requestId, 400);
					return std::nullopt;
				}
				return value;
			};
			const auto page = parse(QStringLiteral("page"), 1, std::numeric_limits<int>::max());
			const auto pageSize = parse(QStringLiteral("pageSize"), 20, 100);
			if (!page.has_value() || !pageSize.has_value()) {
				return std::nullopt;
			}
			result.page = *page;
			result.pageSize = *pageSize;
			return result;
		}

		QJsonObject pageMeta(const Pagination &pagination, qint64 total) {
			return QJsonObject{
				{QStringLiteral("page"), pagination.page},
				{QStringLiteral("pageSize"), pagination.pageSize},
				{QStringLiteral("total"), total},
				{QStringLiteral("hasNext"), static_cast<qint64>(pagination.page) * pagination.pageSize < total},
			};
		}

		bool validateFilters(const HttpRequest &request, QString *where, QVariantList *bindings, HttpResponse *failure) {
			const QString type = request.query.queryItemValue(QStringLiteral("type"));
			const QString occupancy = request.query.queryItemValue(QStringLiteral("occupancyStatus"));
			const QString operational = request.query.queryItemValue(QStringLiteral("operationalStatus"));
			if (!type.isEmpty() && type != QStringLiteral("fast") && type != QStringLiteral("slow")) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电桩类型筛选无效"), {}, request.requestId, 400);
				return false;
			}
			if (!occupancy.isEmpty() && occupancy != QStringLiteral("available") && occupancy != QStringLiteral("reserved") && occupancy != QStringLiteral("charging")) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("占用状态筛选无效"), {}, request.requestId, 400);
				return false;
			}
			if (!operational.isEmpty() && operational != QStringLiteral("online") && operational != QStringLiteral("fault") && operational != QStringLiteral("offline")) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("运维状态筛选无效"), {}, request.requestId, 400);
				return false;
			}
			for (const auto &[column, value] : {std::pair<QString, QString>{QStringLiteral("type"), type}, {QStringLiteral("occupancy_status"), occupancy}, {QStringLiteral("operational_status"), operational}}) {
				if (!value.isEmpty()) {
					*where += QStringLiteral(" AND ") + column + QStringLiteral("=?");
					bindings->append(value);
				}
			}
			return true;
		}

		HttpResponse listChargers(const HttpRequest &request, const ApiDependencies &dependencies, bool admin, std::optional<qint64> stationId) {
			HttpResponse failure;
			if (admin && !requireAdmin(request, dependencies, false, &failure).has_value()) {
				return failure;
			}
			const auto page = parsePagination(request, &failure);
			if (!page.has_value()) {
				return failure;
			}
			bool includeDeleted = false;
			if (admin && request.query.hasQueryItem(QStringLiteral("includeDeleted"))) {
				const QString raw = request.query.queryItemValue(QStringLiteral("includeDeleted"));
				if (raw != QStringLiteral("true") && raw != QStringLiteral("false")) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("includeDeleted 必须是布尔值"), {}, request.requestId, 400);
				}
				includeDeleted = raw == QStringLiteral("true");
			}
			QString where = includeDeleted ? QStringLiteral(" WHERE 1=1") : QStringLiteral(" WHERE deleted_at IS NULL");
			QVariantList bindings;
			if (stationId.has_value()) {
				where += QStringLiteral(" AND station_id=?");
				bindings.append(*stationId);
			}
			if (!validateFilters(request, &where, &bindings, &failure)) {
				return failure;
			}
			QJsonArray items;
			qint64 total = 0;
			bool stationFound = !stationId.has_value();
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError)) {
					database.rollback();
					return false;
				}
				if (stationId.has_value()) {
					QSqlQuery station(database);
					station.prepare(admin
										? QStringLiteral("SELECT 1 FROM stations WHERE id=?")
										: QStringLiteral("SELECT 1 FROM stations WHERE id=? AND status='active' AND deleted_at IS NULL"));
					station.addBindValue(*stationId);
					if (!station.exec()) {
						*operationError = station.lastError().text();
						database.rollback();
						return false;
					}
					stationFound = station.next();
					if (!stationFound) {
						database.rollback();
						return true;
					}
				}
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM chargers") + where);
				for (const QVariant &binding : bindings) {
					count.addBindValue(binding);
				}
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					database.rollback();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery ids(database);
				ids.prepare(QStringLiteral("SELECT id FROM chargers") + where + QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?"));
				for (const QVariant &binding : bindings) {
					ids.addBindValue(binding);
				}
				ids.addBindValue(page->pageSize);
				ids.addBindValue(static_cast<qint64>(page->page - 1) * page->pageSize);
				if (!ids.exec()) {
					*operationError = ids.lastError().text();
					database.rollback();
					return false;
				}
				while (ids.next()) {
					QJsonObject charger;
					bool found = false;
					if (!loadChargerJson(database, ids.value(0).toLongLong(), includeDeleted, &charger, &found, operationError)) {
						database.rollback();
						return false;
					}
					if (found) {
						items.append(charger);
					}
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (!stationFound) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			return jsonData(items, request.requestId, 200, pageMeta(*page, total));
		}

		HttpResponse chargerDetail(const HttpRequest &request, const ApiDependencies &dependencies, bool admin) {
			HttpResponse failure;
			if (admin) {
				if (!requireAdmin(request, dependencies, false, &failure).has_value()) {
					return failure;
				}
			} else {
				const auto principal = authenticate(request, dependencies, &failure);
				if (!principal.has_value()) {
					return failure;
				}
				const bool allowed = principal->role == QStringLiteral("USER") || (principal->status == QStringLiteral("active") && (principal->role == QStringLiteral("ADMIN") || principal->role == QStringLiteral("ADMIN_READONLY")));
				if (!allowed) {
					return jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问此资源"), {}, request.requestId, 403);
				}
			}
			const auto chargerId = positiveId(request.pathParameters.value(QStringLiteral("chargerId")));
			if (!chargerId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			QJsonObject charger;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError) || !loadChargerJson(database, *chargerId, false, &charger, &found, operationError)) {
					database.rollback();
					return false;
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(charger, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
		}

		HttpResponse createCharger(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			if (!stationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QString type = body->value(QStringLiteral("type")).toString();
			const auto powerW = EvCharger::parsePowerWatts(body->value(QStringLiteral("powerKw")), dependencies.config.maxChargerPowerW);
			QJsonObject details;
			if (!body->value(QStringLiteral("type")).isString() || (type != QStringLiteral("fast") && type != QStringLiteral("slow"))) {
				details.insert(QStringLiteral("type"), QStringLiteral("必须是 fast 或 slow"));
			}
			if (!powerW.has_value()) {
				details.insert(QStringLiteral("powerKw"), QStringLiteral("必须是上限内且最多三位小数的正数"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电桩参数无效"), details, request.requestId, 400);
			}
			bool stationFound = false;
			QJsonObject result;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery station(database);
				station.prepare(QStringLiteral("SELECT 1 FROM stations WHERE id=? AND deleted_at IS NULL"));
				station.addBindValue(*stationId);
				if (!station.exec()) {
					*operationError = station.lastError().text();
					database.rollback();
					return false;
				}
				stationFound = station.next();
				if (!stationFound) {
					database.rollback();
					return true;
				}
				QSqlQuery insert(database);
				insert.prepare(QStringLiteral("INSERT INTO chargers(station_id,type,power_w,occupancy_status,operational_status,created_at,updated_at) VALUES (?,?,?,'available','online',?,?)"));
				insert.addBindValue(*stationId);
				insert.addBindValue(type);
				insert.addBindValue(*powerW);
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now));
				if (!insert.exec()) {
					*operationError = insert.lastError().text();
					database.rollback();
					return false;
				}
				bool found = false;
				if (!loadChargerJson(database, insert.lastInsertId().toLongLong(), false, &result, &found, operationError) || !found || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return stationFound ? jsonData(result, request.requestId, 201) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		}

		HttpResponse updateCharger(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto chargerId = positiveId(request.pathParameters.value(QStringLiteral("chargerId")));
			if (!chargerId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const bool hasType = body->contains(QStringLiteral("type"));
			const bool hasPower = body->contains(QStringLiteral("powerKw"));
			const bool hasOperational = body->contains(QStringLiteral("operationalStatus"));
			if (body->contains(QStringLiteral("stationId")) || body->contains(QStringLiteral("occupancyStatus"))) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("不允许直接修改所属电站或占用状态"), {}, request.requestId, 400);
			}
			if (!hasType && !hasPower && !hasOperational) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("至少提供一个可修改字段"), {}, request.requestId, 400);
			}
			const QString type = body->value(QStringLiteral("type")).toString();
			const auto powerW = hasPower ? EvCharger::parsePowerWatts(body->value(QStringLiteral("powerKw")), dependencies.config.maxChargerPowerW) : std::optional<qint64>();
			const QString operational = body->value(QStringLiteral("operationalStatus")).toString();
			QJsonObject details;
			if (hasType && (!body->value(QStringLiteral("type")).isString() || (type != QStringLiteral("fast") && type != QStringLiteral("slow")))) {
				details.insert(QStringLiteral("type"), QStringLiteral("非法类型"));
			}
			if (hasPower && !powerW.has_value()) {
				details.insert(QStringLiteral("powerKw"), QStringLiteral("非法功率"));
			}
			if (hasOperational && (!body->value(QStringLiteral("operationalStatus")).isString() || (operational != QStringLiteral("online") && operational != QStringLiteral("fault") && operational != QStringLiteral("offline")))) {
				details.insert(QStringLiteral("operationalStatus"), QStringLiteral("非法状态"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("电桩参数无效"), details, request.requestId, 400);
			}
			bool found = false;
			QJsonObject result;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT type,power_w,occupancy_status,operational_status FROM chargers WHERE id=? AND deleted_at IS NULL"));
				current.addBindValue(*chargerId);
				if (!current.exec()) {
					*operationError = current.lastError().text();
					database.rollback();
					return false;
				}
				if (!current.next()) {
					database.rollback();
					return true;
				}
				found = true;
				const QString newType = hasType ? type : current.value(0).toString();
				const qint64 newPower = hasPower ? *powerW : current.value(1).toLongLong();
				const QString occupancy = current.value(2).toString();
				const QString newOperational = hasOperational ? operational : current.value(3).toString();
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE chargers SET type=?,power_w=?,operational_status=?,updated_at=? WHERE id=?"));
				update.addBindValue(newType);
				update.addBindValue(newPower);
				update.addBindValue(newOperational);
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(*chargerId);
				if (!update.exec()) {
					*operationError = update.lastError().text();
					database.rollback();
					return false;
				}
				if (occupancy == QStringLiteral("reserved") && newOperational != QStringLiteral("online")) {
					QSqlQuery expire(database);
					expire.prepare(QStringLiteral("UPDATE reservations SET status='expired',updated_at=? WHERE charger_id=? AND status='active'"));
					expire.addBindValue(toDatabaseTimestamp(now));
					expire.addBindValue(*chargerId);
					QSqlQuery release(database);
					release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE id=?"));
					release.addBindValue(toDatabaseTimestamp(now));
					release.addBindValue(*chargerId);
					if (!expire.exec() || !release.exec()) {
						*operationError = expire.lastError().text();
						database.rollback();
						return false;
					}
				}
				if (!loadChargerJson(database, *chargerId, false, &result, &found, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
		}

		HttpResponse restartCharger(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			if (!request.body.trimmed().isEmpty()) {
				const auto body = parseJsonObject(request, &failure);
				if (!body.has_value()) {
					return failure;
				}
				if (!body->isEmpty()) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("重启请求正文必须为空对象"), {}, request.requestId, 400);
				}
			}
			const auto chargerId = positiveId(request.pathParameters.value(QStringLiteral("chargerId")));
			if (!chargerId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			bool found = false;
			bool invalidState = false;
			QJsonObject result;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE chargers SET operational_status='online',updated_at=? WHERE id=? AND deleted_at IS NULL AND occupancy_status='available' AND operational_status IN ('fault','offline')"));
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(*chargerId);
				if (!update.exec()) {
					*operationError = update.lastError().text();
					database.rollback();
					return false;
				}
				QJsonObject current;
				if (!loadChargerJson(database, *chargerId, false, &current, &found, operationError)) {
					database.rollback();
					return false;
				}
				if (found && update.numRowsAffected() == 0) {
					invalidState = true;
					database.rollback();
					return true;
				}
				result = current;
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (!found) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			return invalidState ? jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("当前电桩状态不允许重启"), {}, request.requestId, 409) : jsonData(result, request.requestId);
		}

		HttpResponse deleteCharger(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, true, &failure).has_value()) {
				return failure;
			}
			const auto chargerId = positiveId(request.pathParameters.value(QStringLiteral("chargerId")));
			if (!chargerId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			bool found = false;
			bool blocked = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT deleted_at FROM chargers WHERE id=?"));
				current.addBindValue(*chargerId);
				if (!current.exec()) {
					*operationError = current.lastError().text();
					database.rollback();
					return false;
				}
				if (!current.next()) {
					database.rollback();
					return true;
				}
				found = true;
				if (!current.isNull(0)) {
					database.rollback();
					return true;
				}
				QSqlQuery conflict(database);
				conflict.prepare(QStringLiteral("SELECT 1 FROM reservations WHERE charger_id=? AND status='active' UNION ALL SELECT 1 FROM orders WHERE charger_id=? AND status='charging' LIMIT 1"));
				conflict.addBindValue(*chargerId);
				conflict.addBindValue(*chargerId);
				if (!conflict.exec()) {
					*operationError = conflict.lastError().text();
					database.rollback();
					return false;
				}
				if (conflict.next()) {
					blocked = true;
					database.rollback();
					return true;
				}
				QSqlQuery remove(database);
				remove.prepare(QStringLiteral("UPDATE chargers SET deleted_at=?,updated_at=? WHERE id=?"));
				remove.addBindValue(toDatabaseTimestamp(now));
				remove.addBindValue(toDatabaseTimestamp(now));
				remove.addBindValue(*chargerId);
				if (!remove.exec() || !commitTransaction(database)) {
					*operationError = remove.lastError().text();
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (!found) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电桩不存在"), {}, request.requestId, 404);
			}
			if (blocked) {
				return jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("电桩存在有效预约或正在充电订单"), {}, request.requestId, 409);
			}
			return HttpResponse{204, {}, {}, {}};
		}

	} // namespace

	void registerChargerRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/stations/{stationId}/chargers"), [dependencies](const HttpRequest &request) {
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			return stationId.has_value() ? listChargers(request, dependencies, false, stationId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		});
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/chargers/{chargerId}"), [dependencies](const HttpRequest &request) { return chargerDetail(request, dependencies, false); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/stations/{stationId}/chargers"), [dependencies](const HttpRequest &request) {
			const auto stationId = positiveId(request.pathParameters.value(QStringLiteral("stationId")));
			return stationId.has_value() ? listChargers(request, dependencies, true, stationId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("电站不存在"), {}, request.requestId, 404);
		});
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/chargers"), [dependencies](const HttpRequest &request) {
			std::optional<qint64> stationId;
			if (request.query.hasQueryItem(QStringLiteral("stationId"))) {
				stationId = positiveId(request.query.queryItemValue(QStringLiteral("stationId")));
				if (!stationId.has_value()) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("stationId 无效"), {}, request.requestId, 400);
				}
			}
			return listChargers(request, dependencies, true, stationId);
		});
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/stations/{stationId}/chargers"), [dependencies](const HttpRequest &request) { return createCharger(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/chargers/{chargerId}"), [dependencies](const HttpRequest &request) { return chargerDetail(request, dependencies, true); });
		router.add(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/chargers/{chargerId}"), [dependencies](const HttpRequest &request) { return updateCharger(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/admin/chargers/{chargerId}/restart"), [dependencies](const HttpRequest &request) { return restartCharger(request, dependencies); });
		router.add(QStringLiteral("DELETE"), QStringLiteral("/api/v1/admin/chargers/{chargerId}"), [dependencies](const HttpRequest &request) { return deleteCharger(request, dependencies); });
	}

} // namespace Backend
