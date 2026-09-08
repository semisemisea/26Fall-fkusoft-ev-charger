/**
 * @file validation.h
 * @brief 手机号、文本、功率与显式时区时间的领域输入校验。
 */

#ifndef EVCHARGER_VALIDATION_H
#define EVCHARGER_VALIDATION_H

#include <QDateTime>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace EvCharger {

	/**
	 * @brief 检查手机号是否恰好由 11 个 ASCII 数字组成，不进行运营商号段验证。
	 * @param phone 由 11 个 ASCII 数字组成的手机号文本。
	 * @return 文本恰好为 11 个 ASCII 数字时返回 true，否则返回 false。
	 */
	bool isPhoneNumber(const QString &phone);
	/**
	 * @brief 去除首尾空白后检查闭区间长度，若提供输出指针则同时保存规范化文本。
	 * @param value 待校验、舍入或转换的输入值。
	 * @param minimum 允许的最小值，包含边界。
	 * @param maximum 允许的最大值，包含边界。
	 * @param[out] trimmed 可为空；非空时无论长度校验是否成功都写入去掉首尾空白的文本。
	 * @return 去空白后的长度处于 minimum 到 maximum 闭区间时返回 true，否则返回 false。
	 */
	bool hasTrimmedLength(const QString &value, qsizetype minimum, qsizetype maximum, QString *trimmed = nullptr);
	/**
	 * @brief 将 JSON 千瓦数转换为整数瓦；拒绝非数字、非正数、超限或不能表示为整数瓦的输入。
	 * @param value 待校验、舍入或转换的输入值。
	 * @param maximumWatts 允许的最大功率，单位瓦。
	 * @return 精确换算的正整数瓦数；类型、精度、正值或最大功率校验不通过时返回 std::nullopt。
	 */
	std::optional<qint64> parsePowerWatts(const QJsonValue &value, qint64 maximumWatts);
	/**
	 * @brief 解析带 Z 或显式时区偏移的 ISO 时间并转换为 UTC；不接受缺少时区的本地时间。
	 * @param value 待校验、舍入或转换的输入值。
	 * @return 转换为 UTC 的有效时间；缺少显式时区后缀或时间解析失败时返回 std::nullopt。
	 */
	std::optional<QDateTime> parseExplicitRfc3339(const QString &value);

} // namespace EvCharger

#endif
