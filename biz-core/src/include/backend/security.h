/**
 * @file security.h
 * @brief 随机凭据生成、加盐密码散列及令牌摘要工具。
 */

#ifndef BACKEND_SECURITY_H
#define BACKEND_SECURITY_H

#include <QByteArray>
#include <QString>

namespace Backend::Security {

	/**
	 * @brief 从系统随机源逐字节生成随机数据，用于盐值和访问令牌。
	 * @param size 要生成的字节数，调用方应传入非负值。
	 * @return 长度等于 size 的随机字节串。
	 */
	QByteArray randomBytes(qsizetype size);
	/**
	 * @brief 使用盐值执行 50000 轮 SHA-256，生成与本项目密码存储格式一致的摘要。
	 * @param password 待散列或验证的明文密码。
	 * @param salt 密码盐值，验证时必须使用存储时的相同字节。
	 * @return 32 字节 SHA-256 迭代摘要。
	 */
	QByteArray passwordHash(const QString &password, const QByteArray &salt);
	/**
	 * @brief 重新计算密码摘要；等长摘要按全部字节累积差异后比较。
	 * @param password 待散列或验证的明文密码。
	 * @param salt 密码盐值，验证时必须使用存储时的相同字节。
	 * @param expectedHash 存储的密码摘要。
	 * @return 计算摘要与 expectedHash 长度及全部字节一致返回 true，否则返回 false。
	 */
	bool verifyPassword(const QString &password, const QByteArray &salt, const QByteArray &expectedHash);
	/**
	 * @brief 生成 32 个随机字节并编码为无填充的 URL 安全 Base64 令牌。
	 * @return 无等号填充的 URL 安全 Base64 令牌文本。
	 */
	QString generateAccessToken();
	/**
	 * @brief 计算 UTF-8 令牌的 SHA-256 摘要，数据库只保存此摘要。
	 * @param token 明文访问令牌，仅用于生成摘要或返回客户端。
	 * @return 32 字节 SHA-256 令牌摘要。
	 */
	QByteArray tokenHash(const QString &token);

} // namespace Backend::Security

#endif
