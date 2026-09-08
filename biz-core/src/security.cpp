/**
 * @file security.cpp
 * @brief 随机凭据生成、加盐密码散列及令牌摘要工具。
 */

#include "backend/security.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace Backend::Security {

	/**
	 * @brief 从系统随机源逐字节生成随机数据，用于盐值和访问令牌。
	 * @param size 要生成的字节数，调用方应传入非负值。
	 * @return 长度等于 size 的随机字节串。
	 */
	QByteArray randomBytes(qsizetype size) {
		QByteArray bytes(size, Qt::Uninitialized);
		for (qsizetype offset = 0; offset < size; ++offset) {
			bytes[offset] = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
		}
		return bytes;
	}

	/**
	 * @brief 使用盐值执行 50000 轮 SHA-256，生成与本项目密码存储格式一致的摘要。
	 * @param password 待散列或验证的明文密码。
	 * @param salt 密码盐值，验证时必须使用存储时的相同字节。
	 * @return 32 字节 SHA-256 迭代摘要。
	 */
	QByteArray passwordHash(const QString &password, const QByteArray &salt) {
		QByteArray value = salt + password.toUtf8();
		for (int iteration = 0; iteration < 50'000; ++iteration) {
			value = QCryptographicHash::hash(value + salt, QCryptographicHash::Sha256);
		}
		return value;
	}

	/**
	 * @brief 重新计算密码摘要；等长摘要按全部字节累积差异后比较。
	 * @param password 待散列或验证的明文密码。
	 * @param salt 密码盐值，验证时必须使用存储时的相同字节。
	 * @param expectedHash 存储的密码摘要。
	 * @return 计算摘要与 expectedHash 长度及全部字节一致返回 true，否则返回 false。
	 */
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

	/**
	 * @brief 生成 32 个随机字节并编码为无填充的 URL 安全 Base64 令牌。
	 * @return 无等号填充的 URL 安全 Base64 令牌文本。
	 */
	QString generateAccessToken() {
		return QString::fromLatin1(randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	}

	/**
	 * @brief 计算 UTF-8 令牌的 SHA-256 摘要，数据库只保存此摘要。
	 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
	 * @return 32 字节 SHA-256 令牌摘要。
	 */
	QByteArray tokenHash(const QString &token) {
		return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
	}

} // namespace Backend::Security
