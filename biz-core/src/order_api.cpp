#include "evcharger/logging.h"

#include "api_support.h"

#include "order_support.h"
#include "resource_support.h"

#include "backend/database.h"
#include "evcharger/charging.h"
#include "evcharger/clock.h"
#include "evcharger/validation.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <optional>

Q_LOGGING_CATEGORY(backendOrders, "evcharger.backend.orders", QtInfoMsg)

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
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问订单"), {}, request.requestId, 403);
				return std::nullopt;
			}
			if (active && principal->status == QStringLiteral("frozen")) {
				*failure = jsonError(QStringLiteral("USER_FROZEN"), QStringLiteral("用户已被冻结"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		std::optional<qint64> positiveId(const QJsonValue &value) {
			const qint64 id = value.toInteger(-1);
			return value.isDouble() && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		std::optional<qint64> positiveId(const QString &value) {
			bool ok = false;
			const qint64 id = value.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		bool validIdempotencyKey(const HttpRequest &request) {
			const QByteArray key = request.headers.value(QByteArrayLiteral("idempotency-key"));
			return !key.isEmpty() && key.size() <= 200;
		}

		std::optional<QJsonObject> parseEmptyObject(const HttpRequest &request, HttpResponse *failure) {
			if (request.body.trimmed().isEmpty()) {
				return QJsonObject{};
			}
			const auto body = parseJsonObject(request, failure);
			if (!body.has_value()) {
				return std::nullopt;
			}
			if (!body->isEmpty()) {
				*failure = jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("请求正文必须为空对象"), {}, request.requestId, 400);
				return std::nullopt;
			}
			return body;
		}

		HttpResponse idempotencyConflict(const HttpRequest &request) {
			return jsonError(QStringLiteral("IDEMPOTENCY_KEY_REUSED"), QStringLiteral("幂等键已用于不同请求"), {}, request.requestId, 409);
		}

		HttpResponse startOrder(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling startOrder" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const auto chargerId = positiveId(body->value(QStringLiteral("chargerId")));
			const bool hasReservationId = body->contains(QStringLiteral("reservationId"));
			const auto reservationId = hasReservationId ? positiveId(body->value(QStringLiteral("reservationId"))) : std::optional<qint64>();
			QJsonObject details;
			if (!chargerId.has_value()) {
				details.insert(QStringLiteral("chargerId"), QStringLiteral("必须是正整数"));
			}
			if (hasReservationId && !reservationId.has_value()) {
				details.insert(QStringLiteral("reservationId"), QStringLiteral("必须是正整数"));
			}
			if (!validIdempotencyKey(request)) {
				details.insert(QStringLiteral("Idempotency-Key"), QStringLiteral("必须提供"));
			}
			if (!details.isEmpty()) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("开始充电参数无效"), details, request.requestId, 400);
			}
			QJsonObject normalized{{QStringLiteral("chargerId"), *chargerId}};
			if (reservationId.has_value()) {
				normalized.insert(QStringLiteral("reservationId"), *reservationId);
			}
			const QDateTime now = dependencies.clock->nowUtc();
			IdempotencyResult idempotency;
			QString businessCode;
			QJsonObject result;
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
				auto failBusiness = [&](const QString &code, const QString &message) {
					businessCode = code;
					if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 409, idempotencyError(code, message), operationError) || !commitTransaction(database)) {
						database.rollback();
						return false;
					}
					return true;
				};
				QSqlQuery openOrder(database);
				openOrder.prepare(QStringLiteral("SELECT 1 FROM orders WHERE user_id=? AND status IN ('charging','awaiting_payment') LIMIT 1"));
				openOrder.addBindValue(principal->id);
				if (!openOrder.exec()) {
					*operationError = openOrder.lastError().text();
					database.rollback();
					return false;
				}
				if (openOrder.next()) {
					return failBusiness(QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("用户已有未完成订单"));
				}
				QSqlQuery activeReservation(database);
				activeReservation.prepare(QStringLiteral("SELECT id,charger_id FROM reservations WHERE user_id=? AND status='active' LIMIT 1"));
				activeReservation.addBindValue(principal->id);
				if (!activeReservation.exec()) {
					*operationError = activeReservation.lastError().text();
					database.rollback();
					return false;
				}
				const bool hasActiveReservation = activeReservation.next();
				if ((hasActiveReservation && (!reservationId.has_value() || activeReservation.value(0).toLongLong() != *reservationId || activeReservation.value(1).toLongLong() != *chargerId)) || (!hasActiveReservation && reservationId.has_value())) {
					return failBusiness(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("预约与启动请求不匹配"));
				}
				QSqlQuery charger(database);
				charger.prepare(QStringLiteral("SELECT c.station_id,c.power_w,s.price_fen_per_kwh,c.occupancy_status FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=? AND c.deleted_at IS NULL AND s.deleted_at IS NULL AND s.status='active' AND c.operational_status='online'"));
				charger.addBindValue(*chargerId);
				if (!charger.exec()) {
					*operationError = charger.lastError().text();
					database.rollback();
					return false;
				}
				const QString expectedOccupancy = hasActiveReservation ? QStringLiteral("reserved") : QStringLiteral("available");
				if (!charger.next() || charger.value(3).toString() != expectedOccupancy) {
					return failBusiness(QStringLiteral("CHARGER_UNAVAILABLE"), QStringLiteral("电桩当前不可用"));
				}
				const qint64 stationId = charger.value(0).toLongLong();
				const qint64 powerW = charger.value(1).toLongLong();
				const qint64 price = charger.value(2).toLongLong();
				QSqlQuery occupy(database);
				occupy.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='charging',updated_at=? WHERE id=? AND occupancy_status=? AND operational_status='online'"));
				occupy.addBindValue(toDatabaseTimestamp(now));
				occupy.addBindValue(*chargerId);
				occupy.addBindValue(expectedOccupancy);
				if (!occupy.exec()) {
					*operationError = occupy.lastError().text();
					database.rollback();
					return false;
				}
				if (occupy.numRowsAffected() != 1) {
					return failBusiness(QStringLiteral("CHARGER_UNAVAILABLE"), QStringLiteral("电桩当前不可用"));
				}
				QSqlQuery insert(database);
				insert.prepare(QStringLiteral("INSERT INTO orders(user_id,station_id,charger_id,reservation_id,status,power_w,unit_price_fen_per_kwh,started_at,created_at,updated_at) VALUES (?,?,?,?,'charging',?,?,?,?,?)"));
				insert.addBindValue(principal->id);
				insert.addBindValue(stationId);
				insert.addBindValue(*chargerId);
				insert.addBindValue(reservationId.has_value() ? QVariant(*reservationId) : QVariant());
				insert.addBindValue(powerW);
				insert.addBindValue(price);
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now));
				insert.addBindValue(toDatabaseTimestamp(now));
				if (!insert.exec()) {
					*operationError = insert.lastError().text();
					database.rollback();
					return false;
				}
				if (reservationId.has_value()) {
					QSqlQuery use(database);
					use.prepare(QStringLiteral("UPDATE reservations SET status='used',updated_at=? WHERE id=? AND status='active'"));
					use.addBindValue(toDatabaseTimestamp(now));
					use.addBindValue(*reservationId);
					if (!use.exec() || use.numRowsAffected() != 1) {
						*operationError = use.lastError().text();
						database.rollback();
						return false;
					}
				}
				bool found = false;
				if (!loadOrderJson(database, insert.lastInsertId().toLongLong(), principal->id, now, &result, &found, operationError) || !found || !storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 201, result, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (idempotency.state == IdempotencyState::Reused) {
				return idempotencyConflict(request);
			}
			if (idempotency.state == IdempotencyState::Replay) {
				return replayIdempotency(idempotency, request.requestId);
			}
			if (!businessCode.isEmpty()) {
				const QString message = businessCode == QStringLiteral("ACTIVE_ORDER_EXISTS")	? QStringLiteral("用户已有未完成订单")
										: businessCode == QStringLiteral("CHARGER_UNAVAILABLE") ? QStringLiteral("电桩当前不可用")
																								: QStringLiteral("预约与启动请求不匹配");
				return jsonError(businessCode, message, {}, request.requestId, 409);
			}
			EV_LOG_INFO(backendOrders, nullptr) << "Resource created" << "requestId=" << request.requestId << "resourceId=" << result.value(QStringLiteral("id")).toInteger();
			return jsonData(result, request.requestId, 201);
		}

		HttpResponse activeOrder(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling activeOrder" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery id(database);
				id.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=? AND status IN ('charging','awaiting_payment') LIMIT 1"));
				id.addBindValue(principal->id);
				if (!id.exec()) {
					*operationError = id.lastError().text();
					return false;
				}
				if (!id.next()) {
					return true;
				}
				return loadOrderJson(database, id.value(0).toLongLong(), principal->id, dependencies.clock->nowUtc(), &result, &found, operationError);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return jsonData(found ? QJsonValue(result) : QJsonValue(QJsonValue::Null), request.requestId);
		}

		HttpResponse orderDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling orderDetail" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = authenticate(request, dependencies, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			if ((principal->role != QStringLiteral("USER") && principal->role != QStringLiteral("ADMIN")) || (principal->role == QStringLiteral("ADMIN") && principal->status != QStringLiteral("active"))) {
				return jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问订单"), {}, request.requestId, 403);
			}
			const auto orderId = positiveId(request.pathParameters.value(QStringLiteral("orderId")));
			if (!orderId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
			}
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const std::optional<qint64> owner = principal->role == QStringLiteral("USER") ? std::optional<qint64>(principal->id) : std::nullopt;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				return loadOrderJson(database, *orderId, owner, dependencies.clock->nowUtc(), &result, &found, operationError);
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
		}

		HttpResponse stopOrder(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling stopOrder" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			if (!parseEmptyObject(request, &failure).has_value()) {
				return failure;
			}
			if (!validIdempotencyKey(request)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("必须提供 Idempotency-Key"), {}, request.requestId, 400);
			}
			const auto orderId = positiveId(request.pathParameters.value(QStringLiteral("orderId")));
			if (!orderId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
			}
			const QJsonObject normalized;
			const QDateTime now = dependencies.clock->nowUtc();
			IdempotencyResult idempotency;
			QJsonObject result;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !checkIdempotency(database, *principal, request, normalized, now, &idempotency, operationError)) {
					database.rollback();
					return false;
				}
				if (idempotency.state != IdempotencyState::Missing) {
					database.rollback();
					return true;
				}
				QSqlQuery current(database);
				current.prepare(QStringLiteral("SELECT charger_id,status,power_w,unit_price_fen_per_kwh,started_at FROM orders WHERE id=? AND user_id=?"));
				current.addBindValue(*orderId);
				current.addBindValue(principal->id);
				if (!current.exec()) {
					*operationError = current.lastError().text();
					database.rollback();
					return false;
				}
				if (!current.next()) {
					if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 404, idempotencyError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在")), operationError) || !commitTransaction(database)) {
						database.rollback();
						return false;
					}
					return true;
				}
				found = true;
				if (current.value(1).toString() == QStringLiteral("charging")) {
					const auto metrics = EvCharger::calculateCharge(current.value(2).toLongLong(), current.value(3).toLongLong(), QDateTime::fromString(current.value(4).toString(), Qt::ISODateWithMs), now);
					if (!metrics.has_value()) {
						*operationError = QStringLiteral("Unable to calculate final charge");
						database.rollback();
						return false;
					}
					QSqlQuery totals(database);
					totals.prepare(QStringLiteral("SELECT total_charge_count,total_charge_seconds FROM chargers WHERE id=?"));
					totals.addBindValue(current.value(0));
					if (!totals.exec() || !totals.next()) {
						*operationError = totals.lastError().text();
						database.rollback();
						return false;
					}
					const qint64 count = totals.value(0).toLongLong();
					const qint64 seconds = totals.value(1).toLongLong();
					if (count == std::numeric_limits<qint64>::max() || seconds > std::numeric_limits<qint64>::max() - metrics->durationSeconds) {
						*operationError = QStringLiteral("Charger totals overflow");
						database.rollback();
						return false;
					}
					QSqlQuery updateOrder(database);
					updateOrder.prepare(QStringLiteral("UPDATE orders SET status='awaiting_payment',stopped_at=?,duration_seconds=?,energy_ws=?,amount_fen=?,updated_at=? WHERE id=? AND status='charging'"));
					updateOrder.addBindValue(toDatabaseTimestamp(now));
					updateOrder.addBindValue(metrics->durationSeconds);
					updateOrder.addBindValue(metrics->energyWs);
					updateOrder.addBindValue(metrics->amountFen);
					updateOrder.addBindValue(toDatabaseTimestamp(now));
					updateOrder.addBindValue(*orderId);
					QSqlQuery updateCharger(database);
					updateCharger.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',total_charge_count=?,total_charge_seconds=?,updated_at=? WHERE id=?"));
					updateCharger.addBindValue(count + 1);
					updateCharger.addBindValue(seconds + metrics->durationSeconds);
					updateCharger.addBindValue(toDatabaseTimestamp(now));
					updateCharger.addBindValue(current.value(0));
					if (!updateOrder.exec() || updateOrder.numRowsAffected() != 1 || !updateCharger.exec()) {
						*operationError = updateOrder.lastError().text();
						database.rollback();
						return false;
					}
				}
				if (!loadOrderJson(database, *orderId, principal->id, now, &result, &found, operationError) || !storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 200, result, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (idempotency.state == IdempotencyState::Reused) {
				return idempotencyConflict(request);
			}
			if (idempotency.state == IdempotencyState::Replay) {
				return replayIdempotency(idempotency, request.requestId);
			}
			if (found) {
				EV_LOG_INFO(backendOrders, nullptr) << "Order stop completed" << "requestId=" << request.requestId << "orderId=" << *orderId << "status=" << result.value(QStringLiteral("status")).toString();
			}
			return found ? jsonData(result, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
		}

		HttpResponse settleOrder(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling settleOrder" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			if (body->value(QStringLiteral("paymentMethod")).toString() != QStringLiteral("wallet") || !validIdempotencyKey(request)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("结算参数无效"), {}, request.requestId, 400);
			}
			const auto orderId = positiveId(request.pathParameters.value(QStringLiteral("orderId")));
			if (!orderId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
			}
			const QJsonObject normalized{{QStringLiteral("paymentMethod"), QStringLiteral("wallet")}};
			const QDateTime now = dependencies.clock->nowUtc();
			IdempotencyResult idempotency;
			QJsonObject result;
			bool found = false;
			bool invalidState = false;
			bool insufficient = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database) || !checkIdempotency(database, *principal, request, normalized, now, &idempotency, operationError)) {
					database.rollback();
					return false;
				}
				if (idempotency.state != IdempotencyState::Missing) {
					database.rollback();
					return true;
				}
				auto failBusiness = [&](const QString &code, const QString &message, int status) {
					if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, status, idempotencyError(code, message), operationError) || !commitTransaction(database)) {
						database.rollback();
						return false;
					}
					return true;
				};
				QSqlQuery order(database);
				order.prepare(QStringLiteral("SELECT status,amount_fen FROM orders WHERE id=? AND user_id=?"));
				order.addBindValue(*orderId);
				order.addBindValue(principal->id);
				if (!order.exec()) {
					*operationError = order.lastError().text();
					database.rollback();
					return false;
				}
				if (!order.next()) {
					return failBusiness(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), 404);
				}
				found = true;
				const QString status = order.value(0).toString();
				const qint64 amount = order.value(1).toLongLong();
				qint64 balanceAfter = 0;
				if (status == QStringLiteral("charging")) {
					invalidState = true;
					return failBusiness(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("订单尚未停止"), 409);
				}
				if (status == QStringLiteral("settled")) {
					QSqlQuery debit(database);
					debit.prepare(QStringLiteral("SELECT balance_after_fen FROM wallet_transactions WHERE order_id=? AND type='charge_debit'"));
					debit.addBindValue(*orderId);
					if (!debit.exec() || !debit.next()) {
						*operationError = debit.lastError().text();
						database.rollback();
						return false;
					}
					balanceAfter = debit.value(0).toLongLong();
				} else {
					QSqlQuery user(database);
					user.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id=?"));
					user.addBindValue(principal->id);
					if (!user.exec() || !user.next()) {
						*operationError = user.lastError().text();
						database.rollback();
						return false;
					}
					const qint64 balance = user.value(0).toLongLong();
					if (balance < amount) {
						insufficient = true;
						return failBusiness(QStringLiteral("INSUFFICIENT_BALANCE"), QStringLiteral("钱包余额不足"), 422);
					}
					balanceAfter = balance - amount;
					QSqlQuery updateUser(database);
					updateUser.prepare(QStringLiteral("UPDATE users SET balance_fen=?,updated_at=? WHERE id=?"));
					updateUser.addBindValue(balanceAfter);
					updateUser.addBindValue(toDatabaseTimestamp(now));
					updateUser.addBindValue(principal->id);
					QSqlQuery debit(database);
					debit.prepare(QStringLiteral("INSERT INTO wallet_transactions(user_id,order_id,type,amount_fen,balance_after_fen,created_at) VALUES (?,?,'charge_debit',?,?,?)"));
					debit.addBindValue(principal->id);
					debit.addBindValue(*orderId);
					debit.addBindValue(amount);
					debit.addBindValue(balanceAfter);
					debit.addBindValue(toDatabaseTimestamp(now));
					QSqlQuery updateOrder(database);
					updateOrder.prepare(QStringLiteral("UPDATE orders SET status='settled',settled_at=?,updated_at=? WHERE id=? AND status='awaiting_payment'"));
					updateOrder.addBindValue(toDatabaseTimestamp(now));
					updateOrder.addBindValue(toDatabaseTimestamp(now));
					updateOrder.addBindValue(*orderId);
					if (!updateUser.exec() || !debit.exec() || !updateOrder.exec() || updateOrder.numRowsAffected() != 1) {
						*operationError = updateOrder.lastError().text();
						database.rollback();
						return false;
					}
				}
				QJsonObject orderJson;
				if (!loadOrderJson(database, *orderId, principal->id, now, &orderJson, &found, operationError)) {
					database.rollback();
					return false;
				}
				result = QJsonObject{{QStringLiteral("order"), orderJson}, {QStringLiteral("balanceAfterFen"), balanceAfter}};
				if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, 200, result, operationError) || !commitTransaction(database)) {
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (idempotency.state == IdempotencyState::Reused) {
				return idempotencyConflict(request);
			}
			if (idempotency.state == IdempotencyState::Replay) {
				return replayIdempotency(idempotency, request.requestId);
			}
			if (!found) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("订单不存在"), {}, request.requestId, 404);
			}
			if (invalidState) {
				return jsonError(QStringLiteral("INVALID_STATE_TRANSITION"), QStringLiteral("订单尚未停止"), {}, request.requestId, 409);
			}
			if (insufficient) {
				return jsonError(QStringLiteral("INSUFFICIENT_BALANCE"), QStringLiteral("钱包余额不足"), {}, request.requestId, 422);
			}
			EV_LOG_INFO(backendOrders, nullptr) << "Order settlement completed" << "requestId=" << request.requestId << "orderId=" << *orderId;
			return jsonData(result, request.requestId);
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
			const auto size = parse(QStringLiteral("pageSize"), 20, 100);
			if (!page.has_value() || !size.has_value()) {
				return std::nullopt;
			}
			result.page = *page;
			result.pageSize = *size;
			return result;
		}

		HttpResponse listOrders(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendOrders, nullptr) << "Handling listOrders" << "requestId=" << request.requestId;
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
			if (!status.isEmpty() && status != QStringLiteral("charging") && status != QStringLiteral("awaiting_payment") && status != QStringLiteral("settled")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("订单状态筛选无效"), {}, request.requestId, 400);
			}
			const bool hasFrom = request.query.hasQueryItem(QStringLiteral("from"));
			const bool hasTo = request.query.hasQueryItem(QStringLiteral("to"));
			std::optional<QDateTime> from;
			std::optional<QDateTime> to;
			if (hasFrom != hasTo || (hasFrom && (!(from = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("from")))).has_value() || !(to = EvCharger::parseExplicitRfc3339(request.query.queryItemValue(QStringLiteral("to")))).has_value() || *from >= *to))) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("订单时间区间无效"), {}, request.requestId, 400);
			}
			QString filter;
			QVariantList bindings;
			if (!status.isEmpty()) {
				filter += QStringLiteral(" AND status=?");
				bindings.append(status);
			}
			if (from.has_value()) {
				filter += QStringLiteral(" AND created_at>=? AND created_at<?");
				bindings.append(toDatabaseTimestamp(*from));
				bindings.append(toDatabaseTimestamp(*to));
			}
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE user_id=?") + filter);
				count.addBindValue(principal->id);
				for (const QVariant &value : bindings) {
					count.addBindValue(value);
				}
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery ids(database);
				ids.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=?") + filter + QStringLiteral(" ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				ids.addBindValue(principal->id);
				for (const QVariant &value : bindings) {
					ids.addBindValue(value);
				}
				ids.addBindValue(page->pageSize);
				ids.addBindValue(static_cast<qint64>(page->page - 1) * page->pageSize);
				if (!ids.exec()) {
					*operationError = ids.lastError().text();
					return false;
				}
				while (ids.next()) {
					QJsonObject order;
					bool found = false;
					if (!loadOrderJson(database, ids.value(0).toLongLong(), principal->id, dependencies.clock->nowUtc(), &order, &found, operationError)) {
						return false;
					}
					if (found) {
						items.append(order);
					}
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return jsonData(items, request.requestId, 200, QJsonObject{{QStringLiteral("page"), page->page}, {QStringLiteral("pageSize"), page->pageSize}, {QStringLiteral("total"), total}, {QStringLiteral("hasNext"), static_cast<qint64>(page->page) * page->pageSize < total}});
		}

	} // namespace

	void registerOrderRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/me/active-order"), [dependencies](const HttpRequest &request) { return activeOrder(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/orders"), [dependencies](const HttpRequest &request) { return startOrder(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/orders"), [dependencies](const HttpRequest &request) { return listOrders(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/orders/{orderId}"), [dependencies](const HttpRequest &request) { return orderDetail(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/{orderId}/stop"), [dependencies](const HttpRequest &request) { return stopOrder(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/orders/{orderId}/settle"), [dependencies](const HttpRequest &request) { return settleOrder(request, dependencies); });
	}

} // namespace Backend
