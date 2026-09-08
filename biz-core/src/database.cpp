/**
 * @file database.cpp
 * @brief SQLite 连接作用域、模式初始化与启动时业务状态恢复。
 */

#include "backend/database.h"

#include "backend/security.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <utility>

namespace Backend {
	namespace {

		/**
		 * @brief 执行 SQL 语句，失败时保存驱动报告的错误。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param sql 要执行的 SQL 语句。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return SQL 执行成功返回 true；失败返回 false 并写入驱动错误。
		 */
		bool execute(QSqlDatabase &database, const QString &sql, QString *errorMessage) {
			QSqlQuery query(database);
			if (query.exec(sql)) {
				return true;
			}
			*errorMessage = query.lastError().text();
			return false;
		}

		/**
		 * @brief 通过 Qt SQL 驱动开始数据库事务。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 事务开始成功返回 true，否则返回 false 并写入驱动错误。
		 */
		bool begin(QSqlDatabase &database, QString *errorMessage) {
			if (database.transaction()) {
				return true;
			}
			*errorMessage = database.lastError().text();
			return false;
		}

		/**
		 * @brief 提交事务；失败时回滚并报告 SQL 错误。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 事务提交成功返回 true；失败返回 false、写入错误并尝试回滚。
		 */
		bool commit(QSqlDatabase &database, QString *errorMessage) {
			if (database.commit()) {
				return true;
			}
			*errorMessage = database.lastError().text();
			database.rollback();
			return false;
		}

		/**
		 * @brief 回滚当前事务并返回 false，供迁移失败分支统一退出。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @return 始终返回 false，表示当前操作失败。
		 */
		bool failTransaction(QSqlDatabase &database) {
			database.rollback();
			return false;
		}

		/**
		 * @brief 返回版本 1 的建表和索引语句，其中部分唯一索引限制活动预约、未完成订单与扣款次数。
		 * @return 静态模式语句列表的只读引用，在进程生命周期内有效。
		 */
		const QStringList &migrationStatements() {
			static const QStringList statements = {
				QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, phone TEXT NOT NULL UNIQUE, nickname TEXT NOT NULL, avatar BLOB, avatar_mime TEXT, balance_fen INTEGER NOT NULL DEFAULT 0 CHECK(balance_fen >= 0), status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','frozen')), created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS admins (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE, display_name TEXT NOT NULL, password_salt BLOB NOT NULL, password_hash BLOB NOT NULL, role TEXT NOT NULL CHECK(role IN ('ADMIN','ADMIN_READONLY')), status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','disabled')), created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS access_tokens (id INTEGER PRIMARY KEY AUTOINCREMENT, token_hash BLOB NOT NULL UNIQUE, principal_type TEXT NOT NULL CHECK(principal_type IN ('user','admin')), principal_id INTEGER NOT NULL, role TEXT NOT NULL CHECK(role IN ('USER','ADMIN','ADMIN_READONLY')), created_at TEXT NOT NULL, expires_at TEXT NOT NULL, revoked_at TEXT)"),
				QStringLiteral("CREATE INDEX IF NOT EXISTS access_tokens_lookup ON access_tokens(token_hash, expires_at, revoked_at)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS service_credentials (service_name TEXT PRIMARY KEY, token_hash BLOB NOT NULL, role TEXT NOT NULL CHECK(role = 'SERVICE'), updated_at TEXT NOT NULL)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS stations (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, latitude REAL NOT NULL CHECK(latitude BETWEEN -90 AND 90), longitude REAL NOT NULL CHECK(longitude BETWEEN -180 AND 180), price_fen_per_kwh INTEGER NOT NULL CHECK(price_fen_per_kwh > 0), status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','inactive')), created_at TEXT NOT NULL, updated_at TEXT NOT NULL, deleted_at TEXT)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS chargers (id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL REFERENCES stations(id), type TEXT NOT NULL CHECK(type IN ('fast','slow')), power_w INTEGER NOT NULL CHECK(power_w > 0), occupancy_status TEXT NOT NULL DEFAULT 'available' CHECK(occupancy_status IN ('available','reserved','charging')), operational_status TEXT NOT NULL DEFAULT 'online' CHECK(operational_status IN ('online','fault','offline')), total_charge_count INTEGER NOT NULL DEFAULT 0 CHECK(total_charge_count >= 0), total_charge_seconds INTEGER NOT NULL DEFAULT 0 CHECK(total_charge_seconds >= 0), created_at TEXT NOT NULL, updated_at TEXT NOT NULL, deleted_at TEXT)"),
				QStringLiteral("CREATE INDEX IF NOT EXISTS chargers_station ON chargers(station_id)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS reservations (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL REFERENCES users(id), station_id INTEGER NOT NULL REFERENCES stations(id), charger_id INTEGER NOT NULL REFERENCES chargers(id), status TEXT NOT NULL CHECK(status IN ('active','used','cancelled','expired')), created_at TEXT NOT NULL, expires_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
				QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS one_active_reservation_per_user ON reservations(user_id) WHERE status = 'active'"),
				QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS one_active_reservation_per_charger ON reservations(charger_id) WHERE status = 'active'"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS orders (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL REFERENCES users(id), station_id INTEGER NOT NULL REFERENCES stations(id), charger_id INTEGER NOT NULL REFERENCES chargers(id), reservation_id INTEGER REFERENCES reservations(id), status TEXT NOT NULL CHECK(status IN ('charging','awaiting_payment','settled')), power_w INTEGER NOT NULL CHECK(power_w > 0), unit_price_fen_per_kwh INTEGER NOT NULL CHECK(unit_price_fen_per_kwh > 0), started_at TEXT NOT NULL, stopped_at TEXT, settled_at TEXT, duration_seconds INTEGER, energy_ws INTEGER, amount_fen INTEGER, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
				QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS one_open_order_per_user ON orders(user_id) WHERE status IN ('charging','awaiting_payment')"),
				QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS one_charging_order_per_charger ON orders(charger_id) WHERE status = 'charging'"),
				QStringLiteral("CREATE INDEX IF NOT EXISTS orders_created ON orders(created_at DESC, id DESC)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS wallet_transactions (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL REFERENCES users(id), order_id INTEGER REFERENCES orders(id), type TEXT NOT NULL CHECK(type IN ('top_up','charge_debit')), amount_fen INTEGER NOT NULL, balance_after_fen INTEGER NOT NULL CHECK(balance_after_fen >= 0), created_at TEXT NOT NULL)"),
				QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS one_charge_debit_per_order ON wallet_transactions(order_id) WHERE type = 'charge_debit'"),
				QStringLiteral("CREATE INDEX IF NOT EXISTS wallet_transactions_user ON wallet_transactions(user_id, created_at DESC, id DESC)"),
				QStringLiteral("CREATE TABLE IF NOT EXISTS idempotency_records (id INTEGER PRIMARY KEY AUTOINCREMENT, principal_type TEXT NOT NULL, principal_id INTEGER NOT NULL, method TEXT NOT NULL, path TEXT NOT NULL, idempotency_key TEXT NOT NULL, request_hash BLOB NOT NULL, http_status INTEGER NOT NULL, response_data BLOB NOT NULL, created_at TEXT NOT NULL, expires_at TEXT NOT NULL, UNIQUE(principal_type, principal_id, method, path, idempotency_key))"),
				QStringLiteral("CREATE INDEX IF NOT EXISTS idempotency_expiry ON idempotency_records(expires_at)"),
			};
			return statements;
		}

	} // namespace

	/**
	 * @brief 保存数据库路径和锁等待期限，连接在每次操作时创建。
	 * @param path SQLite 数据库文件路径。
	 * @param busyTimeoutMs SQLite 锁争用等待上限，单位毫秒。
	 */
	Database::Database(QString path, int busyTimeoutMs)
		: m_path(std::move(path)), m_busyTimeoutMs(busyTimeoutMs) {
	}

	/**
	 * @brief 转换为包含毫秒的 UTC ISO 字符串，用作数据库时间文本。
	 * @param dateTime 需要转为数据库 UTC 文本的时刻。
	 * @return UTC ISO 8601 毫秒时间文本。
	 */
	QString toDatabaseTimestamp(const QDateTime &dateTime) {
		return dateTime.toUTC().toString(Qt::ISODateWithMs);
	}

	/**
	 * @brief 在调用线程创建独立 SQLite 连接，启用外键、WAL 和忙等待后执行回调，结束时关闭并移除连接。
	 * @param operation 在连接有效期内同步执行的回调；不得让查询或连接逃逸此作用域。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
	 * @return 连接配置成功且回调返回 true 时返回 true；打开/配置失败或回调返回 false 时返回 false。
	 * @note 回调负责事务提交或回滚，不得在回调结束后继续使用连接和查询。
	 */
	bool Database::withConnection(const Operation &operation, QString *errorMessage) const {
		QString ignoredError;
		if (errorMessage == nullptr) {
			errorMessage = &ignoredError;
		}
		errorMessage->clear();

		const QString connectionName = QStringLiteral("ev-charger-%1-%2")
										   .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
										   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		bool result = false;
		{
			QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
			database.setDatabaseName(m_path);
			if (!database.open()) {
				*errorMessage = database.lastError().text();
			} else {
				QSqlQuery pragma(database);
				const bool configured = pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")) && pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL")) && pragma.exec(QStringLiteral("PRAGMA busy_timeout = %1").arg(m_busyTimeoutMs));
				if (!configured) {
					*errorMessage = pragma.lastError().text();
				} else {
					result = operation(database, errorMessage);
				}
				database.close();
			}
		}
		// 所有 QSqlQuery 和 QSqlDatabase 局部句柄已销毁，才可安全移除 Qt 连接注册项。
		QSqlDatabase::removeDatabase(connectionName);
		return result;
	}

	/**
	 * @brief 创建数据库目录，依次执行模式迁移、一致性检查、状态恢复及服务凭据更新。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param serviceToken 服务身份令牌；空值表示不启用该身份。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
	 * @return 目录、迁移、一致性、恢复及服务配置全部成功返回 true；任一步失败返回 false。
	 */
	bool Database::initialize(const QDateTime &nowUtc, const QString &serviceToken, QString *errorMessage) const {
		const QFileInfo databaseFile(m_path);
		if (!QDir().mkpath(databaseFile.absolutePath())) {
			if (errorMessage != nullptr) {
				*errorMessage = QStringLiteral("Unable to create database directory");
			}
			return false;
		}
		return withConnection([&](QSqlDatabase &database, QString *operationError) {
			return migrate(database, nowUtc, operationError) && validateConsistency(database, operationError) && recoverState(database, nowUtc, operationError) && configureService(database, serviceToken, nowUtc, operationError);
		},
							  errorMessage);
	}

	/**
	 * @brief 在事务中创建版本 1 模式和缺失的默认管理员，拒绝不支持的模式版本。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 模式版本有效且建表/默认管理员事务提交成功返回 true；SQL 失败或不支持的版本返回 false。
	 */
	bool Database::migrate(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const {
		if (!begin(database, errorMessage)) {
			return false;
		}
		for (const QString &statement : migrationStatements()) {
			if (!execute(database, statement, errorMessage)) {
				return failTransaction(database);
			}
		}

		QSqlQuery version(database);
		if (!version.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1"))) {
			*errorMessage = version.lastError().text();
			return failTransaction(database);
		}
		if (version.next()) {
			if (version.value(0).toInt() != 1) {
				*errorMessage = QStringLiteral("Unsupported database schema version");
				return failTransaction(database);
			}
		} else if (!execute(database, QStringLiteral("INSERT INTO schema_version(version) VALUES (1)"), errorMessage)) {
			return failTransaction(database);
		}

		QSqlQuery existingAdmin(database);
		if (!existingAdmin.exec(QStringLiteral("SELECT 1 FROM admins WHERE username = 'admin' LIMIT 1"))) {
			*errorMessage = existingAdmin.lastError().text();
			return failTransaction(database);
		}
		if (!existingAdmin.next()) {
			const QByteArray salt = Security::randomBytes(16);
			QSqlQuery insertAdmin(database);
			insertAdmin.prepare(QStringLiteral("INSERT INTO admins(username, display_name, password_salt, password_hash, role, status, created_at, updated_at) VALUES ('admin', '系统管理员', ?, ?, 'ADMIN', 'active', ?, ?)"));
			insertAdmin.addBindValue(salt);
			insertAdmin.addBindValue(Security::passwordHash(QStringLiteral("123456"), salt));
			insertAdmin.addBindValue(toDatabaseTimestamp(nowUtc));
			insertAdmin.addBindValue(toDatabaseTimestamp(nowUtc));
			if (!insertAdmin.exec()) {
				*errorMessage = insertAdmin.lastError().text();
				return failTransaction(database);
			}
		}
		return commit(database, errorMessage);
	}

	/**
	 * @brief 检查预约与未完成订单之间的用户、充电桩独占关系及充电桩有效性。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 所有占用一致性查询均成功且未发现冲突返回 true；SQL 失败或发现业务冲突返回 false。
	 */
	bool Database::validateConsistency(QSqlDatabase &database, QString *errorMessage) const {
		const QStringList checks = {
			QStringLiteral("SELECT user_id FROM orders WHERE status IN ('charging','awaiting_payment') GROUP BY user_id HAVING COUNT(*) > 1 LIMIT 1"),
			QStringLiteral("SELECT charger_id FROM orders WHERE status = 'charging' GROUP BY charger_id HAVING COUNT(*) > 1 LIMIT 1"),
			QStringLiteral("SELECT user_id FROM reservations WHERE status = 'active' GROUP BY user_id HAVING COUNT(*) > 1 LIMIT 1"),
			QStringLiteral("SELECT charger_id FROM reservations WHERE status = 'active' GROUP BY charger_id HAVING COUNT(*) > 1 LIMIT 1"),
			QStringLiteral("SELECT r.id FROM reservations r JOIN orders o ON o.user_id=r.user_id WHERE r.status='active' AND o.status IN ('charging','awaiting_payment') LIMIT 1"),
			QStringLiteral("SELECT r.id FROM reservations r JOIN orders o ON o.charger_id=r.charger_id WHERE r.status='active' AND o.status='charging' LIMIT 1"),
			QStringLiteral("SELECT o.id FROM orders o LEFT JOIN chargers c ON c.id = o.charger_id WHERE o.status = 'charging' AND (c.id IS NULL OR c.deleted_at IS NOT NULL) LIMIT 1"),
		};
		for (const QString &sql : checks) {
			QSqlQuery query(database);
			if (!query.exec(sql)) {
				*errorMessage = query.lastError().text();
				return false;
			}
			if (query.next()) {
				*errorMessage = QStringLiteral("Database business consistency check failed");
				return false;
			}
		}
		return true;
	}

	/**
	 * @brief 启动时释放预约占用；到期预约设为 expired，其余活动预约设为 cancelled，并恢复充电中桩占用。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 预约释放、预约终止和充电占用恢复全部提交成功返回 true；失败回滚并返回 false。
	 */
	bool Database::recoverState(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const {
		if (!begin(database, errorMessage)) {
			return false;
		}
		const QString now = toDatabaseTimestamp(nowUtc);
		QSqlQuery release(database);
		release.prepare(QStringLiteral("UPDATE chargers SET occupancy_status = 'available', updated_at = ? WHERE id IN (SELECT charger_id FROM reservations WHERE status = 'active') AND occupancy_status = 'reserved'"));
		release.addBindValue(now);
		if (!release.exec()) {
			*errorMessage = release.lastError().text();
			return failTransaction(database);
		}
		QSqlQuery expire(database);
		expire.prepare(QStringLiteral("UPDATE reservations SET status = CASE WHEN expires_at <= ? THEN 'expired' ELSE 'cancelled' END, updated_at = ? WHERE status = 'active'"));
		expire.addBindValue(now);
		expire.addBindValue(now);
		if (!expire.exec()) {
			*errorMessage = expire.lastError().text();
			return failTransaction(database);
		}
		QSqlQuery restore(database);
		restore.prepare(QStringLiteral("UPDATE chargers SET occupancy_status = 'charging', updated_at = ? WHERE id IN (SELECT charger_id FROM orders WHERE status = 'charging')"));
		restore.addBindValue(now);
		if (!restore.exec()) {
			*errorMessage = restore.lastError().text();
			return failTransaction(database);
		}
		return commit(database, errorMessage);
	}

	/**
	 * @brief 在事务中替换服务凭据；配置为空时清除服务身份，否则保存令牌摘要。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param serviceToken 服务身份令牌；空值表示不启用该身份。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 旧凭据清理和可选新凭据保存提交成功返回 true；失败回滚并返回 false。
	 */
	bool Database::configureService(QSqlDatabase &database, const QString &serviceToken, const QDateTime &nowUtc, QString *errorMessage) const {
		if (!begin(database, errorMessage)) {
			return false;
		}
		if (!execute(database, QStringLiteral("DELETE FROM service_credentials"), errorMessage)) {
			return failTransaction(database);
		}
		if (!serviceToken.isEmpty()) {
			QSqlQuery insert(database);
			insert.prepare(QStringLiteral("INSERT INTO service_credentials(service_name, token_hash, role, updated_at) VALUES ('ml', ?, 'SERVICE', ?)"));
			insert.addBindValue(Security::tokenHash(serviceToken));
			insert.addBindValue(toDatabaseTimestamp(nowUtc));
			if (!insert.exec()) {
				*errorMessage = insert.lastError().text();
				return failTransaction(database);
			}
		}
		return commit(database, errorMessage);
	}

} // namespace Backend
