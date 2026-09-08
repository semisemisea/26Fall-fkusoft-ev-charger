/**
 * @file user_api.cpp
 * @brief 个人资料、头像、钱包余额及幂等充值接口。
 */
#include "evcharger/logging.h"

#include "api_support.h"

#include "backend/database.h"
#include "evcharger/clock.h"
#include "evcharger/validation.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>

Q_LOGGING_CATEGORY(backendUsers, "evcharger.backend.users", QtInfoMsg)

namespace Backend {
	namespace {

		/** @brief 经参数校验的页码和页大小，用于 LIMIT/OFFSET 和响应元数据。 */
		struct Pagination {
			int page = 1;	   ///< 从 1 开始的页码。
			int pageSize = 20; ///< 每页记录数。
		};

		/**
		 * @brief 验证调用者为普通用户，并按当前操作要求检查账号是否处于 active 状态。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @param requireActive 是否禁止冻结用户执行此操作。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 通过用户类型及所需账号状态检查的主体；认证失败或不满足权限条件时返回 std::nullopt，并写入 failure。
		 */
		std::optional<Principal> requireUser(const HttpRequest &request, const ApiDependencies &dependencies, bool requireActive, HttpResponse *failure) {
			const auto principal = authenticate(request, dependencies, failure);
			if (!principal.has_value()) {
				return std::nullopt;
			}
			if (principal->type != QStringLiteral("user")) {
				*failure = jsonError(QStringLiteral("FORBIDDEN"), QStringLiteral("当前身份无权访问此资源"), {}, request.requestId, 403);
				return std::nullopt;
			}
			if (requireActive && principal->status == QStringLiteral("frozen")) {
				*failure = jsonError(QStringLiteral("USER_FROZEN"), QStringLiteral("用户已被冻结"), {}, request.requestId, 403);
				return std::nullopt;
			}
			return principal;
		}

		/**
		 * @brief 解析页码和每页数量，使用接口默认值并拒绝超出范围的输入。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
		 * @return 包含默认值或已校验参数的分页值；非法页码/页大小返回 std::nullopt，并写入 failure。
		 */
		std::optional<Pagination> parsePagination(const HttpRequest &request, HttpResponse *failure) {
			Pagination pagination;
			auto parse = [&](const QString &name, int defaultValue, int maximum) -> std::optional<int> {
				if (!request.query.hasQueryItem(name)) {
					return defaultValue;
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

		/**
		 * @brief 读取当前用户资料、头像存在标志与余额。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse getProfile(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling getProfile" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			QJsonObject profile;
			bool found = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,phone,nickname,avatar IS NOT NULL,balance_fen,status,created_at FROM users WHERE id = ?"));
				query.addBindValue(principal->id);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				if (query.next()) {
					found = true;
					profile = userJson(query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString());
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return found ? jsonData(profile, request.requestId) : jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"), {}, request.requestId, 404);
		}

		/**
		 * @brief 校验并更新当前用户昵称，返回最新个人资料。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse updateProfile(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling updateProfile" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			QString nickname;
			if (!body->value(QStringLiteral("nickname")).isString() || !EvCharger::hasTrimmedLength(body->value(QStringLiteral("nickname")).toString(), 1, 30, &nickname)) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("请求参数无效"), QJsonObject{{QStringLiteral("nickname"), QStringLiteral("去除首尾空白后须为 1 至 30 个字符")}}, request.requestId, 400);
			}
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery query(database);
				query.prepare(QStringLiteral("UPDATE users SET nickname = ?, updated_at = ? WHERE id = ?"));
				query.addBindValue(nickname);
				query.addBindValue(toDatabaseTimestamp(dependencies.clock->nowUtc()));
				query.addBindValue(principal->id);
				if (query.exec()) {
					return true;
				}
				*operationError = query.lastError().text();
				return false;
			},
																	   &databaseError);
			return success ? getProfile(request, dependencies) : databaseFailure(request.requestId, databaseError);
		}

		/** @brief 解析后的单个头像文件，包含媒体类型和原始字节。 */
		struct AvatarPart {
			QByteArray mimeType; ///< JPEG 或 PNG 头像媒体类型。
			QByteArray bytes;	 ///< 头像文件原始字节。
		};

		/**
		 * @brief 解析单文件 multipart 头像内容，校验边界、文件字段与 JPEG/PNG 媒体类型。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @return 媒体类型和文件字节；multipart 边界、文件部分头或图片媒体类型不符合约定时返回 std::nullopt。
		 */
		std::optional<AvatarPart> parseAvatar(const HttpRequest &request) {
			const QByteArray contentType = request.headers.value(QByteArrayLiteral("content-type"));
			static const QRegularExpression boundaryExpression(QStringLiteral("^multipart/form-data\\s*;\\s*boundary=(?:\"([^\"]+)\"|([^;\\s]+))$"), QRegularExpression::CaseInsensitiveOption);
			const QRegularExpressionMatch match = boundaryExpression.match(QString::fromLatin1(contentType));
			if (!match.hasMatch()) {
				return std::nullopt;
			}
			const QByteArray boundary = (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toLatin1();
			if (boundary.isEmpty() || boundary.size() > 200) {
				return std::nullopt;
			}
			const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
			if (!request.body.startsWith(delimiter + QByteArrayLiteral("\r\n")) || !request.body.endsWith(QByteArrayLiteral("\r\n") + delimiter + QByteArrayLiteral("--\r\n"))) {
				return std::nullopt;
			}
			const qsizetype headersStart = delimiter.size() + 2;
			const qsizetype headersEnd = request.body.indexOf(QByteArrayLiteral("\r\n\r\n"), headersStart);
			if (headersEnd < 0) {
				return std::nullopt;
			}
			const qsizetype dataStart = headersEnd + 4;
			const qsizetype dataEnd = request.body.indexOf(QByteArrayLiteral("\r\n") + delimiter, dataStart);
			if (dataEnd < 0 || request.body.indexOf(delimiter, dataEnd + delimiter.size()) >= 0) {
				return std::nullopt;
			}
			QByteArray mimeType;
			bool fileDisposition = false;
			for (QByteArray line : request.body.mid(headersStart, headersEnd - headersStart).split('\n')) {
				line = line.trimmed();
				const qsizetype separator = line.indexOf(':');
				if (separator <= 0) {
					return std::nullopt;
				}
				const QByteArray name = line.left(separator).trimmed().toLower();
				const QByteArray value = line.mid(separator + 1).trimmed();
				if (name == QByteArrayLiteral("content-type")) {
					mimeType = value.toLower();
				} else if (name == QByteArrayLiteral("content-disposition")) {
					fileDisposition = value.toLower().contains(QByteArrayLiteral("form-data")) && value.contains(QByteArrayLiteral("name=\"file\""));
				}
			}
			if (!fileDisposition || (mimeType != QByteArrayLiteral("image/jpeg") && mimeType != QByteArrayLiteral("image/png"))) {
				return std::nullopt;
			}
			return AvatarPart{mimeType, request.body.mid(dataStart, dataEnd - dataStart)};
		}

		/**
		 * @brief 验证活动用户及头像大小后，在事务中保存头像字节与媒体类型。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse uploadAvatar(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling uploadAvatar" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto avatar = parseAvatar(request);
			if (!avatar.has_value() || avatar->bytes.size() > dependencies.config.avatarBodyLimitBytes) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("头像表单无效"), QJsonObject{{QStringLiteral("file"), QStringLiteral("只接受单个 JPEG 或 PNG 文件")}}, request.requestId, 400);
			}
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database)) {
					return false;
				}
				QSqlQuery query(database);
				query.prepare(QStringLiteral("UPDATE users SET avatar = ?, avatar_mime = ?, updated_at = ? WHERE id = ?"));
				query.addBindValue(avatar->bytes);
				query.addBindValue(QString::fromLatin1(avatar->mimeType));
				query.addBindValue(toDatabaseTimestamp(dependencies.clock->nowUtc()));
				query.addBindValue(principal->id);
				if (!query.exec() || !commitTransaction(database)) {
					*operationError = query.lastError().text();
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			return success ? jsonData(QJsonObject{{QStringLiteral("hasAvatar"), true}, {QStringLiteral("mimeType"), QString::fromLatin1(avatar->mimeType)}}, request.requestId) : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 读取当前用户头像并以其媒体类型返回二进制；不存在时返回资源错误。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse getAvatar(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling getAvatar" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			QByteArray bytes;
			QByteArray mimeType;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT avatar,avatar_mime FROM users WHERE id = ?"));
				query.addBindValue(principal->id);
				if (!query.exec()) {
					*operationError = query.lastError().text();
					return false;
				}
				if (query.next() && !query.isNull(0)) {
					bytes = query.value(0).toByteArray();
					mimeType = query.value(1).toByteArray();
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			return mimeType.isEmpty() ? jsonError(QStringLiteral("NOT_FOUND"), QStringLiteral("尚未上传头像"), {}, request.requestId, 404) : HttpResponse{200, mimeType, bytes, {}};
		}

		/**
		 * @brief 清空当前用户头像数据与媒体类型。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse deleteAvatar(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling deleteAvatar" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, true, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery query(database);
				query.prepare(QStringLiteral("UPDATE users SET avatar = NULL, avatar_mime = NULL, updated_at = ? WHERE id = ?"));
				query.addBindValue(toDatabaseTimestamp(dependencies.clock->nowUtc()));
				query.addBindValue(principal->id);
				if (query.exec()) {
					return true;
				}
				*operationError = query.lastError().text();
				return false;
			},
																	   &databaseError);
			return success ? HttpResponse{204, {}, {}, {}} : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 读取当前用户钱包余额，以分为单位返回。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse getWallet(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling getWallet" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			qint64 balance = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id = ?"));
				query.addBindValue(principal->id);
				if (!query.exec() || !query.next()) {
					*operationError = query.lastError().text();
					return false;
				}
				balance = query.value(0).toLongLong();
				return true;
			},
																	   &databaseError);
			return success ? jsonData(QJsonObject{{QStringLiteral("balanceFen"), balance}}, request.requestId) : databaseFailure(request.requestId, databaseError);
		}

		/**
		 * @brief 按查询列顺序转换钱包流水，保留订单关联及变更后余额。
		 * @param query 已定位到目标记录的 SQL 查询。
		 * @return 符合本接口字段约定的 JSON 数据。
		 */
		QJsonObject walletTransactionJson(const QSqlQuery &query) {
			return QJsonObject{
				{QStringLiteral("id"), query.value(0).toLongLong()},
				{QStringLiteral("type"), query.value(1).toString()},
				{QStringLiteral("amountFen"), query.value(2).toLongLong()},
				{QStringLiteral("balanceAfterFen"), query.value(3).toLongLong()},
				{QStringLiteral("createdAt"), query.value(4).toString()},
			};
		}

		/**
		 * @brief 分页读取当前用户钱包流水并返回总数。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse listWalletTransactions(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling listWalletTransactions" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto pagination = parsePagination(request, &failure);
			if (!pagination.has_value()) {
				return failure;
			}
			QJsonArray items;
			qint64 total = 0;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				QSqlQuery count(database);
				count.prepare(QStringLiteral("SELECT COUNT(*) FROM wallet_transactions WHERE user_id = ?"));
				count.addBindValue(principal->id);
				if (!count.exec() || !count.next()) {
					*operationError = count.lastError().text();
					return false;
				}
				total = count.value(0).toLongLong();
				QSqlQuery query(database);
				query.prepare(QStringLiteral("SELECT id,type,amount_fen,balance_after_fen,created_at FROM wallet_transactions WHERE user_id = ? ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?"));
				query.addBindValue(principal->id);
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
			return jsonData(items, request.requestId, 200, QJsonObject{
															   {QStringLiteral("page"), pagination->page},
															   {QStringLiteral("pageSize"), pagination->pageSize},
															   {QStringLiteral("total"), total},
															   {QStringLiteral("hasNext"), static_cast<qint64>(pagination->page) * pagination->pageSize < total},
														   });
		}

		/**
		 * @brief 校验充值金额和幂等键，在同一事务内更新余额、记录流水并保存重放结果。
		 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
		 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
		 * @return 成功数据或对应的校验、权限、业务冲突、数据库错误响应。
		 */
		HttpResponse topUp(const HttpRequest &request, const ApiDependencies &dependencies) {
			EV_LOG_DEBUG(backendUsers, nullptr) << "Handling topUp" << "requestId=" << request.requestId;
			HttpResponse failure;
			const auto principal = requireUser(request, dependencies, false, &failure);
			if (!principal.has_value()) {
				return failure;
			}
			const auto body = parseJsonObject(request, &failure);
			if (!body.has_value()) {
				return failure;
			}
			const QJsonValue amountValue = body->value(QStringLiteral("amountFen"));
			const qint64 amount = amountValue.toInteger(-1);
			if (!amountValue.isDouble() || amount < dependencies.config.topUpMinFen || amount > dependencies.config.topUpMaxFen) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("充值金额无效"), QJsonObject{{QStringLiteral("amountFen"), QStringLiteral("必须是配置范围内的整数分")}}, request.requestId, 400);
			}
			const QByteArray idempotencyKey = request.headers.value(QByteArrayLiteral("idempotency-key"));
			if (idempotencyKey.isEmpty() || idempotencyKey.size() > 200) {
				return jsonError(QStringLiteral("VALIDATION_ERROR"), QStringLiteral("缺少或无效的 Idempotency-Key"), QJsonObject{{QStringLiteral("Idempotency-Key"), QStringLiteral("必须提供")}}, request.requestId, 400);
			}
			const QJsonObject normalized{{QStringLiteral("amountFen"), amount}};
			const QByteArray requestHash = QCryptographicHash::hash(QJsonDocument(normalized).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
			const QDateTime now = dependencies.clock->nowUtc();
			QJsonObject result;
			int resultStatus = 201;
			HttpResponse businessFailure;
			bool hasBusinessFailure = false;
			QString databaseError;
			const bool success = dependencies.database->withConnection([&](QSqlDatabase &database, QString *operationError) {
				if (!beginTransaction(database)) {
					return false;
				}
				QSqlQuery cleanup(database);
				cleanup.prepare(QStringLiteral("DELETE FROM idempotency_records WHERE expires_at <= ?"));
				cleanup.addBindValue(toDatabaseTimestamp(now));
				if (!cleanup.exec()) {
					*operationError = cleanup.lastError().text();
					database.rollback();
					return false;
				}
				QSqlQuery existing(database);
				existing.prepare(QStringLiteral("SELECT request_hash,http_status,response_data FROM idempotency_records WHERE principal_type = 'user' AND principal_id = ? AND method = 'POST' AND path = ? AND idempotency_key = ?"));
				existing.addBindValue(principal->id);
				existing.addBindValue(request.path);
				existing.addBindValue(QString::fromUtf8(idempotencyKey));
				if (!existing.exec()) {
					*operationError = existing.lastError().text();
					database.rollback();
					return false;
				}
				if (existing.next()) {
					database.rollback();
					if (existing.value(0).toByteArray() != requestHash) {
						hasBusinessFailure = true;
						businessFailure = jsonError(QStringLiteral("IDEMPOTENCY_KEY_REUSED"), QStringLiteral("幂等键已用于不同请求"), {}, request.requestId, 409);
						return true;
					}
					resultStatus = existing.value(1).toInt();
					result = QJsonDocument::fromJson(existing.value(2).toByteArray()).object();
					return true;
				}
				QSqlQuery user(database);
				user.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id = ?"));
				user.addBindValue(principal->id);
				if (!user.exec() || !user.next()) {
					*operationError = user.lastError().text();
					database.rollback();
					return false;
				}
				const qint64 balance = user.value(0).toLongLong();
				// 先做减法比较避免 balance + amount 溢出；上限失败也需保存幂等结果。
				if (balance > dependencies.config.maxWalletBalanceFen - amount) {
					resultStatus = 422;
					result = idempotencyError(QStringLiteral("BALANCE_LIMIT_EXCEEDED"), QStringLiteral("充值后余额将超过上限"));
					hasBusinessFailure = true;
					businessFailure = jsonError(QStringLiteral("BALANCE_LIMIT_EXCEEDED"), QStringLiteral("充值后余额将超过上限"), {}, request.requestId, 422);
					if (!storeIdempotency(database, *principal, request, normalized, now, dependencies.config.idempotencyRetentionHours, resultStatus, result, operationError) || !commitTransaction(database)) {
						database.rollback();
						return false;
					}
					return true;
				}
				const qint64 newBalance = balance + amount;
				QSqlQuery update(database);
				update.prepare(QStringLiteral("UPDATE users SET balance_fen = ?, updated_at = ? WHERE id = ?"));
				update.addBindValue(newBalance);
				update.addBindValue(toDatabaseTimestamp(now));
				update.addBindValue(principal->id);
				QSqlQuery transaction(database);
				transaction.prepare(QStringLiteral("INSERT INTO wallet_transactions(user_id,type,amount_fen,balance_after_fen,created_at) VALUES (?,'top_up',?,?,?)"));
				transaction.addBindValue(principal->id);
				transaction.addBindValue(amount);
				transaction.addBindValue(newBalance);
				transaction.addBindValue(toDatabaseTimestamp(now));
				if (!update.exec()) {
					*operationError = update.lastError().text();
					database.rollback();
					return false;
				}
				if (!transaction.exec()) {
					*operationError = transaction.lastError().text();
					database.rollback();
					return false;
				}
				result = QJsonObject{
					{QStringLiteral("id"), transaction.lastInsertId().toLongLong()},
					{QStringLiteral("type"), QStringLiteral("top_up")},
					{QStringLiteral("amountFen"), amount},
					{QStringLiteral("balanceAfterFen"), newBalance},
					{QStringLiteral("createdAt"), toDatabaseTimestamp(now)},
				};
				QSqlQuery remember(database);
				remember.prepare(QStringLiteral("INSERT INTO idempotency_records(principal_type,principal_id,method,path,idempotency_key,request_hash,http_status,response_data,created_at,expires_at) VALUES ('user',?,'POST',?,?,?,?,?,?,?)"));
				remember.addBindValue(principal->id);
				remember.addBindValue(request.path);
				remember.addBindValue(QString::fromUtf8(idempotencyKey));
				remember.addBindValue(requestHash);
				remember.addBindValue(resultStatus);
				remember.addBindValue(QJsonDocument(result).toJson(QJsonDocument::Compact));
				remember.addBindValue(toDatabaseTimestamp(now));
				remember.addBindValue(toDatabaseTimestamp(now.addSecs(static_cast<qint64>(dependencies.config.idempotencyRetentionHours) * 3600)));
				if (!remember.exec() || !commitTransaction(database)) {
					*operationError = remember.lastError().text();
					database.rollback();
					return false;
				}
				return true;
			},
																	   &databaseError);
			if (!success) {
				return databaseFailure(request.requestId, databaseError);
			}
			if (!hasBusinessFailure && result.contains(QStringLiteral("_idempotencyError"))) {
				return replayIdempotency(IdempotencyResult{IdempotencyState::Replay, resultStatus, result}, request.requestId);
			}
			return hasBusinessFailure ? businessFailure : jsonData(result, request.requestId, resultStatus);
		}

	} // namespace

	/**
	 * @brief 注册个人资料和钱包路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerUserRoutes(Router &router, const ApiDependencies &dependencies) {
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/me"), [dependencies](const HttpRequest &request) { return getProfile(request, dependencies); });
		router.add(QStringLiteral("PATCH"), QStringLiteral("/api/v1/me"), [dependencies](const HttpRequest &request) { return updateProfile(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/me/avatar"), [dependencies](const HttpRequest &request) { return uploadAvatar(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/me/avatar"), [dependencies](const HttpRequest &request) { return getAvatar(request, dependencies); });
		router.add(QStringLiteral("DELETE"), QStringLiteral("/api/v1/me/avatar"), [dependencies](const HttpRequest &request) { return deleteAvatar(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet"), [dependencies](const HttpRequest &request) { return getWallet(request, dependencies); });
		router.add(QStringLiteral("GET"), QStringLiteral("/api/v1/me/wallet/transactions"), [dependencies](const HttpRequest &request) { return listWalletTransactions(request, dependencies); });
		router.add(QStringLiteral("POST"), QStringLiteral("/api/v1/me/wallet/topups"), [dependencies](const HttpRequest &request) { return topUp(request, dependencies); });
	}

} // namespace Backend
