#include "backend/security.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace Backend::Security {

	QByteArray randomBytes(qsizetype size) {
		QByteArray bytes(size, Qt::Uninitialized);
		for (qsizetype offset = 0; offset < size; ++offset) {
			bytes[offset] = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
		}
		return bytes;
	}

	QByteArray passwordHash(const QString &password, const QByteArray &salt) {
		QByteArray value = salt + password.toUtf8();
		for (int iteration = 0; iteration < 50'000; ++iteration) {
			value = QCryptographicHash::hash(value + salt, QCryptographicHash::Sha256);
		}
		return value;
	}

	bool verifyPassword(const QString &password, const QByteArray &salt, const QByteArray &expectedHash) {
		const QByteArray actualHash = passwordHash(password, salt);
		if (actualHash.size() != expectedHash.size()) {
			return false;
		}
		unsigned char difference = 0;
		for (qsizetype index = 0; index < actualHash.size(); ++index) {
			difference |= static_cast<unsigned char>(actualHash[index]) ^ static_cast<unsigned char>(expectedHash[index]);
		}
		return difference == 0;
	}

	QString generateAccessToken() {
		return QString::fromLatin1(randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	}

	QByteArray tokenHash(const QString &token) {
		return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
	}

} // namespace Backend::Security
