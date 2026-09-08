#include "evcharger/logging.h"

#include "api_support.h"

#include "order_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <optional>

Q_LOGGING_CATEGORY(backendAdminUsers, "evcharger.backend.adminusers", QtInfoMsg)

namespace Backend {
	namespace {

		struct Pagination {
			int page = 1;
			int pageSize = 20;
		};

		std::optional<Principal> requireAdmin(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("admin") || principal->status != QStringLiteral("active") || principal->role != QStringLiteral("ADMIN")) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权管理用户"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		std::optional<qint64> positiveId(const QString &value) {
			bool ok = false;
			const qint64 id = value.toLongLong(&ok);
			return ok && id > 0 ? std::optional<qint64>(id) : std::nullopt;
		}

		std::optional<Pagination> parsePagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination pagination;
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
			pagination.page = *page;
			pagination.pageSize = *pageSize;
			return pagination;
		}

		QJsonObject pageMeta(const Pagination &pagination, qint64 total) {
			return QJsonObject{
				{QStringLiteral("page"), pagination.page},
				{QStringLiteral("pageSize"), pagination.pageSize},
				{QStringLiteral("total"), total},
				{QStringLiteral("hasNext"), static_cast<qint64>(pagination.page) * pagination.pageSize < total},
			};
		}

		bool loadUser(QSqlDatabase &database, qint64 userId, QJsonObject *user, bool *found, QString *errorMessage) {
			QSqlQuery query(database);
			query.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE id=?"));
			query.addBindValue(userId);
			if (!query.exec()) {
				*errorMessage = query.lastError().text();
				return false;
			}
			if (!query.next()) {
				*found = false;
				return true;
			}
			*user = userJson(query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString());
			*found = true;
			return true;
		}

		HttpResponse listUsers(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAdminUsers, nullptr) << "Handling listUsers" << "requestId=" << request.requestId;
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto pagination = parsePagination(request, &failure);
			if (!pagination.has_value()) {
				return failure;
			}
			const QString status = request.query.queryItemValue(QStringLiteral("status"));
			if (!status.isEmpty() && status != QStringLiteral("active") && status != QStringLiteral("frozen")) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("用户状态筛选无效"), QJsonObject{{QStringLiteral("status"), QStringLiteral("不支持的状态")}}, request.requestId, 400);
			}
			const QString phone = request.query.queryItemValue(QStringLiteral("phone"), QUrl::FullyDecoded);
			for (const QChar character : phone) {
				if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
					return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("手机号筛选无效"), QJsonObject{{QStringLiteral("phone"), QStringLiteral("只能包含 ASCII 数字")}}, request.requestId, 400);
				}
			}

			QString filter;
			QVariantList bindings;
			if (!phone.isEmpty()) {
				filter += QStringLiteral(" AND instr(phone,?)>0");
				bindings.append(phone);
			}
			if (!status.isEmpty()) {
				filter += QStringLiteral(" AND status=?");
				bindings.append(status);
			}
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				auto bindFilters = [&](QSqlQuery &query) {
					for (const QVariant &binding : bindings) {
						query.addBindValue(binding);
					}
				};
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE 1=1") + filter);
				bindFilters(count);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE 1=1") + filter + QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?"));
				bindFilters(query);
				query.addBindValue(pagination->pageSize);
				query.addBindValue(static_cast<qint64>(pagination->page - 1) * pagination->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					items.append(userJson(query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString()));
				}
				return true;
			},
																	   &databaseError);
			return success ? jsonData(items, request.requestId, 200, pageMeta(*pagination, total)) : databaseFailure(request.requestId, databaseError);
		}

		HttpResponse userDetail(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAdminUsers, nullptr) << "Handling userDetail" << "requestId=" << request.requestId;
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			QJsonObject user;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!loadUser(database, *userId, &user, &found, operationError)) {
					return false;
				}
				if (!found) {
					return true;
				}
				QSqlQuery recent(database);
				recent.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=? ORDER BY created_at DESC,id DESC LIMIT 1"));
				recent.addBindValue(*userId);
				if (!recent.exec()) {
					*operationError = recent.lastError().text();
					return false;
				}
				if (!recent.next()) {
					user.insert(QStringLiteral("recentOrder"), QJsonValue(QJsonValue::Null));
					return true;
				}
				QJsonObject order;
				bool orderFound = false;
				if (!loadOrderJson(database, recent.value(0).toLongLong(), *userId, dependencies.clock->nowUtc(), &order, &orderFound, operationError) || !orderFound) {
					return false;
				}
				user.insert(QStringLiteral("recentOrder"), order);
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(user, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

		HttpResponse updateUser(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAdminUsers, nullptr) << "Handling updateUser" << "requestId=" << request.requestId;
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QString status = body->value(QStringLiteral("status")).toString();
			if (!body->value(QStringLiteral("status")).isString() || (status != QStringLiteral("active") && status != QStringLiteral("frozen"))) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("用户状态无效"), QJsonObject{{QStringLiteral("status"), QStringLiteral("只接受 active 或 frozen")}}, request.requestId, 400);
			}

			QJsonObject user;
			bool found = false;
			QString databaseError;
			const QDateTime now = dependencies.clock->nowUtc();
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database)) {
					*operationError = database.lastError().text();
					return false;
				}
				QSqlQuery existing(database);
				existing.prepare(QStringLiteral("SELECT 1 FROM users WHERE id=?"));
				existing.addBindValue(*userId);
				if (!existing.exec()) {
					*operationError = existing.lastError().text();
					database.rollback();
					return false;
				}
				if (!existing.next()) {
					database.rollback();
					return true;
				}
				found = true;
				if (status == QStringLiteral("frozen")) {
					QSqlQuery release(database);
					release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status='available',updated_at=? WHERE occupancy_status='reserved' AND id IN (SELECT charger_id FROM reservations WHERE user_id=? AND status='active')"));
					release.addBindValue(toDatabaseTimestamp(now));
					release.addBindValue(*userId);
					QSqlQuery cancel(database);
					cancel.prepare(QStringLiteral("UPDATE reservations SET status='cancelled',updated_at=? WHERE user_id=? AND status='active'"));
					cancel.addBindValue(toDatabaseTimestamp(now));
					cancel.addBindValue(*userId);
					if (!release.exec() || !cancel.exec()) {
						*operationError = !release.lastError().text().isEmpty() ? release.lastError().text() : cancel.lastError().text();
						database.rollback();
						return false;
					}
				}
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE users SET status=?,updated_at=? WHERE id=?"));
				update.addBindValue(status);
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(*userId);
				if (!update.exec() || !loadUser(database, *userId, &user, &found, operationError) || !commitTransaction(database)) {
					if (operationError->isEmpty()) {
						*operationError = update.lastError().text();
					}
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(user, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

		QJsonObject walletTransactionJson(const QSqlQuery &query) {
			return QJsonObject{
				{QStringLiteral("id"), query.value(0).toLongLong()},
				{QStringLiteral("type"), query.value(1).toString()},
				{QStringLiteral("amountFen"), query.value(2).toLongLong()},
				{QStringLiteral("balanceAfterFen"), query.value(3).toLongLong()},
				{QStringLiteral("createdAt"), query.value(4).toString()},
			};
		}

		HttpResponse userWalletTransactions(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendAdminUsers, nullptr) << "Handling userWalletTransactions" << "requestId=" << request.requestId;
			HttpResponse failure;
			if (!requireAdmin(request, dependencies, &failure).has_value()) {
				return failure;
			}
			const auto userId = positiveId(request.pathParameters.value(QStringLiteral("userId")));
			const auto pagination = parsePagination(request, &failure);
			if (!userId.has_value()) {
				return jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
			}
			if (!pagination.has_value()) {
				return failure;
			}

			QJsonArray items;
			qint64 total = 0;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery user(database);
				user.prepare(QStringLiteral("SELECT 1 FROM users WHERE id=?"));
				user.addBindValue(*userId);
				if (!user.exec()) {
					*operationError = user.lastError().text();
					return false;
				}
				if (!user.next()) {
					return true;
				}
				found = true;
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions WHERE user_id=?"));
				count.addBindValue(*userId);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,type,amount_fen,balance_after_fen,created_at FROM wallet_transactions WHERE user_id=? ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				query.addBindValue(*userId);
				query.addBindValue(pagination->pageSize);
				query.addBindValue(static_cast<qint64>(pagination->page - 1) * pagination->pageSize);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				while (query.next()) {
					items.append(walletTransactionJson(query));
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(items, request.requestId, 200, pageMeta(*pagination, total)) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

	} // namespace

	void registerAdminUserRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users"), [dependencies](const HttpRequest &request) { return listUsers(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/{userId}"), [dependencies](const HttpRequest &request) { return userDetail(request, dependencies); });
		router.add(QStringLiteral("PATCH"), QStringLiteral("/api/v1/admin/users/{userId}"), [dependencies](const HttpRequest &request) { return updateUser(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/admin/users/{userId}/wallet/transactions"), [dependencies](const HttpRequest &request) { return userWalletTransactions(request, dependencies); });
	}

} // namespace Backend
