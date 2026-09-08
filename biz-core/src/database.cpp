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

		bool execute(QSqlDatabase &database, const QString &sql, QString *errorMessage) {
			QSqlQuery query(database);
			if (query.exec(sql)) {
				return true;
			}
			*errorMessage = query.lastError().text();
			return false;
		}

		bool begin(QSqlDatabase &database, QString *errorMessage) {
			if (database.transaction()) {
				return true;
			}
			*errorMessage = database.lastError().text();
			return false;
		}

		bool commit(QSqlDatabase &database, QString *errorMessage) {
			if (database.commit()) {
				return true;
			}
			*errorMessage = database.lastError().text();
			database.rollback();
			return false;
		}

		bool failTransaction(QSqlDatabase &database) {
			database.rollback();
			return false;
		}

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

	Database::Database(QString path, int busyTimeoutMs)
		: m_path(std::move(path)), m_busyTimeoutMs(busyTimeoutMs) {
	}

	QString toDatabaseTimestamp(const QDateTime &dateTime) {
		return dateTime.toUTC().toString(Qt::ISODateWithMs);
	}

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
		QSqlDatabase::removeDatabase(connectionName);
		return result;
	}

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
