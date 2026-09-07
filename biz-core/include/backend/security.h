#ifndef BACKEND_SECURITY_H
#define BACKEND_SECURITY_H

#include <QByteArray>
#include <QString>

namespace Backend::Security {

	QByteArray randomBytes(qsizetype size);
	QByteArray passwordHash(const QString &password, const QByteArray &salt);
	bool verifyPassword(const QString &password, const QByteArray &salt, const QByteArray &expectedHash);
	QString generateAccessToken();
	QByteArray tokenHash(const QString &token);

} // namespace Backend::Security

#endif
