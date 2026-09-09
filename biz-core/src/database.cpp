/**
 * @file database.cpp
 * @brief SQLite 连接作用域、模式初始化与启动时业务状态恢复。
 */
#include "evcharger/logging.h"

#include "backend/database.h"

#include "backend/security.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QResource>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <utility>

Q_LOGGING_CATEGORY(backendDatabase, "evcharger.backend.database", QtInfoMsg)

// 静态库需要显式引用资源初始化符号，确保链接器保留 SQL 资源。
static void initializeDatabaseResources() {
	Q_INIT_RESOURCE(database);
}

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

		// 每段恰好一条 SQL；显式分隔符避免误拆字符串或触发器中的分号。
		bool executeMigration(QSqlDatabase &database, const QString &name, QString *errorMessage) {
			QFile file(QStringLiteral(":/backend/database/migrations/") + name);
			if (!file.open(QIODevice::ReadOnly)) {
				*errorMessage = name + QStringLiteral(": ") + file.errorString();
				return false;
			}
			const QString source = QString::fromUtf8(file.readAll());
			const QStringList statements = source.split(QRegularExpression(QStringLiteral("(?m)^-- statement-breakpoint\\r?$")));
			for (int i = 0; i < statements.size(); ++i) {
				if (!statements.at(i).trimmed().isEmpty() && !execute(database, statements.at(i), errorMessage)) {
					*errorMessage = QStringLiteral("%1 statement %2: %3").arg(name).arg(i + 1).arg(*errorMessage);
					return false;
				}
			}
			return true;
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
		if (!result) {
			EV_LOG_CRITICAL(backendDatabase, nullptr) << "Database operation failed" << "reason=" << *errorMessage;
		}
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
		EV_LOG_INFO(backendDatabase, nullptr) << "Initializing database" << "busyTimeoutMs=" << m_busyTimeoutMs;
		const QFileInfo databaseFile(m_path);
		if (!QDir().mkpath(databaseFile.absolutePath())) {
			EV_LOG_CRITICAL(backendDatabase, nullptr) << "Unable to create database directory";
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
		EV_LOG_INFO(backendDatabase, nullptr) << "Applying schema migration";
		if (!begin(database, errorMessage)) {
			return false;
		}
		initializeDatabaseResources();
		if (!executeMigration(database, QStringLiteral("000_metadata.sql"), errorMessage)) {
			return failTransaction(database);
		}
		// 列表位置对应模式版本；后续升级只追加脚本，不修改已发布的迁移。
		const QStringList migrations = {QStringLiteral("001_initial.sql")};
		QSqlQuery version(database);
		if (!version.exec(QStringLiteral("SELECT version FROM schema_version"))) {
			*errorMessage = version.lastError().text();
			return failTransaction(database);
		}
		int currentVersion = 0;
		const bool hasVersion = version.next();
		if (hasVersion) {
			bool valid = false;
			currentVersion = version.value(0).toInt(&valid);
			if (!valid || currentVersion < 1 || currentVersion > migrations.size() || version.next()) {
				*errorMessage = QStringLiteral("Unsupported database schema version");
				return failTransaction(database);
			}
		}
		version.finish();
		for (int i = currentVersion; i < migrations.size(); ++i) {
			if (!executeMigration(database, migrations.at(i), errorMessage)) {
				return failTransaction(database);
			}
			const QString update = (i == 0 && !hasVersion)
									   ? QStringLiteral("INSERT INTO schema_version(version) VALUES (%1)")
									   : QStringLiteral("UPDATE schema_version SET version = %1");
			if (!execute(database, update.arg(i + 1), errorMessage)) {
				return failTransaction(database);
			}
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
		EV_LOG_INFO(backendDatabase, nullptr) << "Checking database consistency";
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
		EV_LOG_INFO(backendDatabase, nullptr) << "Recovering charger and reservation state";
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
		const bool committed = commit(database, errorMessage);
		if (committed) {
			EV_LOG_INFO(backendDatabase, nullptr) << "Startup state recovery committed" << "releasedChargers=" << release.numRowsAffected() << "closedReservations=" << expire.numRowsAffected() << "chargingChargers=" << restore.numRowsAffected();
		}
		return committed;
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
		EV_LOG_INFO(backendDatabase, nullptr) << "Refreshing service credentials";
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
