#include "api_support.h"

#include "resource_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"

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

		std::optional<Principal> requireUser(const HttpRequest &request, const ApiDependencies &dependencies, bool active, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("user")) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问预约"), {}, request.requestId, 403);
				return std::nullopt;
			}
			if (active && principal->status == QStringLiteral("frozen")) {
				*failure = jsonError(QStringLiteral("USER_FROZEN"), QStringLiteral("用户已被冻结"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		std::optional<qint64> positiveInteger(const QJsonValue &value) {
			const qint64 integer = value.toInteger(-1);
			return value.isDouble() && integer > 0 ? std::optional<qint64>(integer) : std::nullopt;
		}

		std::optional<qint64> positiveId(const QString &raw) {
			bool ok = false;
			const qint64 id = raw.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		QJsonObject reservationJson(const QSqlQuery &query) {
			return QJsonObject{
				{QStringLiteral("id"), query.value(0).toLongLong()},
				{QStringLiteral("userId"), query.value(1).toLongLong()},
				{QStringLiteral("stationId"), query.value(2).toLongLong()},
				{QStringLiteral("chargerId"), query.value(3).toLongLong()},
				{QStringLiteral("status"), query.value(4).toString()},
				{QStringLiteral("createdAt"), query.value(5).toString()},
				{QStringLiteral("expiresAt"), query.value(6).toString()},
			};
		}

		bool loadReservation(QSqlDatabase &database, qint64 reservationId, qint64 userId, QJsonObject *reservation, bool *found, QString *errorMessage) {
			QSqlQuery query(database);
			query.prepare(QStringLiteral("SELECT id,user_id,station_id,charger_id,status,created_at,expires_at FROM reservations WHERE id=? AND user_id=?"));
			query.addBindValue(reservationId);
			query.addBindValue(userId);
			if (!query.exec()) {
				*errorMessage = query.lastError().text();
				return false;
			}
			if (!query.next()) {
				*found = false;
				return true;
			}
			*reservation = reservationJson(query);
			*found = true;
			return true;
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
					*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("分页参数无效"), {}, request.requestId, 400);
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

		HttpResponse createReservation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const auto chargerId = positiveInteger(body->value(QStringLiteral("chargerId")));
			const auto holdMinutes = positiveInteger(body->value(QStringLiteral("holdMinutes")));
			QJsonObject details;
			if (!chargerId.has_value()) {
				details.insert(QStringLiteral("chargerId"), QStringLiteral("必须是正整数"));
			}
			if (!holdMinutes.has_value() || *holdMinutes > dependencies.config.maxReservationMinutes) {
				details.insert(QStringLiteral("holdMinutes"), QStringLiteral("超出合法范围"));
			}
			const QByteArray key = request.headers.value(QByteArrayLiteral("idempotency-key"));
			if (key.isEmpty() || key.size() > 200) {
				details.insert(QStringLiteral("Idempotency-Key"), QStringLiteral("必须提供"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("预约参数无效"), details, request.requestId, 400);
			}
			const QJsonObject normalized{{QStringLiteral("chargerId"), *chargerId}, {QStringLiteral("holdMinutes"), *holdMinutes}};
			const QDateTime now = dependencies.clock->nowUtc();
			QJsonObject result;
			IdempotencyResult idempotency;
			QString businessCode;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError) || !checkIdempotency(database, *principal, request, normalized, now, &idempotency, operationError)) {
					database.rollback();
					return false;
				}
				if (idempotency.state != IdempotencyState::Missing) {
					database.rollback();
					return true;
				}
				QSqlQuery active(database);
				active.prepare(QStringLiteral("SELECT 1 FROM reservations WHERE user_id=? AND status='active' LIMIT 1"));
				active.addBindValue(principal->id);
				if (!active.exec()) {
					*operationError = active.lastError().text();
					database.rollback();
					return false;
				}
				if (active.next()) {
					businessCode = QStringLiteral("INVALID_STATE_TRANSITION");
					database.rollback();
					return true;
				}
				QSqlQuery openOrder(database);
				openOrder.prepare(QStringLiteral("SELECT 1 FROM orders WHERE user_id=? AND status IN ('charging','awaiting_payment') LIMIT 1"));
				openOrder.addBindValue(principal->id);
				if (!openOrder.exec()) {
					*operationError = openOrder.lastError().text();
					database.rollback();
					return false;
				}
				if (openOrder.next()) {
					businessCode = QStringLiteral("ACTIVE_ORDER_EXISTS");
					database.rollback();
					return true;
				}
				QSqlQuery charger(database);
				charger.prepare(QStringLiteral("SELECT c.station_id FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=? AND c.deleted_at IS NULL AND s.deleted_at IS NULL AND s.status='active' AND c.occupancy_status='available' AND c.operational_status='online'"));
				charger.addBindValue(*chargerId);
				if (!charger.exec()) {
					*operationError = charger.lastError().text();
					database.rollback();
					return false;
				}
				if (!charger.next()) {
					businessCode = QStringLiteral("CHARGER_UNAVAILABLE");
					database.rollback();
					return true;
				}
				const qint64 stationId = charger.value(0).toLongLong();
				QSqlQuery occupy(database);
				occupy.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='reserved',updated_at=? WHERE id=? AND occupancy_status='available' AND operational_status='online' AND deleted_at IS NULL"));
				occupy.addBindValue(toDatabaseTimestamp(now));
				occupy.addBindValue(*chargerId);
				if (!occupy.exec() || occupy.numRowsAffected() != 1) {
					businessCode = QStringLiteral("CHARGER_UNAVAILABLE");
					database.rollback();
					return true;
				}
				QSqlQuery insert(database);
				insert.prepare(QStringLiteral("INSERT INTO reservations(user_id,station_id,charger_id,status,created_at,expires_at,updated_at) VALUES (?,?,?,'active',?,?,?)"));
				insert.addBindValue(principal->id);
				insert.addBindValue(stationId);
				insert.addBindValue(*chargerId);
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now.addSecs(*holdMinutes * 60)));
				insert.addBindValue(toDatabaseTimestamp(now));
				if (!insert.exec()) {
					*operationError = insert.lastError().text();
					database.rollback();
					return false;
				}
				bool found = false;
				if (!loadReservation(database, insert.lastInsertId().toLongLong(), principal->id, &result, &found, operationError) || !found || !storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 201, result, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			if (idempotency.state == IdempotencyState::Reused) {
				return jsonError(QStringLiteral("IDEMPOTENCY_KEY_REUSED"), QStringLiteral("幂等键已用于不同请求"), {}, request.requestId, 409);
			}
			if (idempotency.state == IdempotencyState::Replay) {
				return jsonData(idempotency.data, request.requestId, idempotency.status);
			}
			if (businessCode == QStringLiteral("ACTIVE_ORDER_EXISTS")) {
				return jsonError(QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("用户已有未完成订单"), {}, request.requestId, 409);
			}
			if (!businessCode.isEmpty()) {
				const int status = businessCode == QStringLiteral("CHARGER_UNAVAILABLE") ? 409 : 409;
				return jsonError(businessCode, businessCode == QStringLiteral("CHARGER_UNAVAILABLE") ? QStringLiteral("电桩当前不可用") : QStringLiteral("当前状态不允许创建预约"), {}, request.requestId, status);
			}
			return jsonData(result, request.requestId, 201);
		}

		HttpResponse listReservations(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto page = parsePagination(request, &failure);
			if (!page.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("active") && status != QStringLiteral("used") && status != QStringLiteral("cancelled") && status != QStringLiteral("expired")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("预约状态筛选无效"), {}, request.requestId, 400);
			}
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError)) {
					database.rollback();
					return false;
				}
				const QString filter = status.isEmpty() ? QString() : QStringLiteral(" AND status=?");
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE user_id=?") + filter);
				count.addBindValue(principal->id);
				if (!status.isEmpty()) {
					count.addBindValue(status);
				}
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					database.rollback();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,user_id,station_id,charger_id,status,created_at,expires_at FROM reservations WHERE user_id=?") + filter + QStringLiteral(" ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				query.addBindValue(principal->id);
				if (!status.isEmpty()) {
					query.addBindValue(status);
				}
				query.addBindValue(page->pageSize);
				query.addBindValue(static_cast<qint64>(page->page - 1) * page->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					database.rollback();
					return false;
				}
				while (query.next()) {
					items.append(reservationJson(query));
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			return jsonData(items, request.requestId, 200, QJsonObject{{QStringLiteral("page"), page->page}, {QStringLiteral("pageSize"), page->pageSize}, {QStringLiteral("total"), total}, {QStringLiteral("hasNext"), static_cast<qint64>(page->page) * page->pageSize < total}});
		}

		HttpResponse reservationDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto reservationId = positiveId(request.pathParameters.value(QStringLiteral("reservationId")));
			if (!reservationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, dependencies.clock->nowUtc(), operationError) || !loadReservation(database, *reservationId, principal->id, &result, &found, operationError)) {
					database.rollback();
					return false;
				}
				return commitTransaction(database);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
		}

		HttpResponse cancelReservation(const HttpRequest &request, const ApiDependencies &dependencies) {
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto reservationId = positiveId(request.pathParameters.value(QStringLiteral("reservationId")));
			if (!reservationId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			QJsonObject result;
			bool found = false;
			bool invalidState = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !expireDueReservations(database, now, operationError)) {
					database.rollback();
					return false;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT charger_id,status FROM reservations WHERE id=? AND user_id=?"));
				current.addBindValue(*reservationId);
				current.addBindValue(principal->id);
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
				if (current.value(1).toString() != QStringLiteral("active")) {
					invalidState = true;
					database.rollback();
					return true;
				}
				const qint64 chargerId = current.value(0).toLongLong();
				QSqlQuery cancel(database);
				cancel.prepare(QStringLiteral("UPDATE reservations SET status='cancelled',updated_at=? WHERE id=? AND status='active'"));
				cancel.addBindValue(toDatabaseTimestamp(now));
				cancel.addBindValue(*reservationId);
				QSqlQuery release(database);
				release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE id=? AND occupancy_status='reserved'"));
				release.addBindValue(toDatabaseTimestamp(now));
				release.addBindValue(chargerId);
				if (!cancel.exec() || !release.exec() || !loadReservation(database, *reservationId, principal->id, &result, &found, operationError) || !commitTransaction(database)) {
					*operationError = cancel.lastError().text();
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId);
			}
			if (!found) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("预约不存在"), {}, request.requestId, 404);
			}
			return invalidState ? jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("预约状态不允许取消"), {}, request.requestId, 409) : jsonData(result, request.requestId);
		}

	} // namespace

	void registerReservationRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"), [dependencies](const HttpRequest &request) { return createReservation(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations"), [dependencies](const HttpRequest &request) { return listReservations(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/reservations/{reservationId}"), [dependencies](const HttpRequest &request) { return reservationDetail(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/reservations/{reservationId}/cancel"), [dependencies](const HttpRequest &request) { return cancelReservation(request, dependencies); });
	}

} // namespace Backend
