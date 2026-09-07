#ifndef BACKEND_DATABASE_H
#define BACKEND_DATABASE_H

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <functional>

namespace Backend {

	class Database {
	public:
		using Operation = std::function<bool(QSqlDatabase &, QString *)>;

		Database(QString path, int busyTimeoutMs);

		bool initialize(const QDateTime &nowUtc, const QString &serviceToken, QString *errorMessage) const;
		bool withConnection(const Operation &operation, QString *errorMessage) const;

	private:
		bool migrate(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const;
		bool validateConsistency(QSqlDatabase &database, QString *errorMessage) const;
		bool recoverState(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const;
		bool configureService(QSqlDatabase &database, const QString &serviceToken, const QDateTime &nowUtc, QString *errorMessage) const;

		QString m_path;
		int m_busyTimeoutMs;
	};

	QString toDatabaseTimestamp(const QDateTime &dateTime);

} // namespace Backend

#endif
