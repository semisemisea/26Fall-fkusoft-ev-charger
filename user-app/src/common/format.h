/**
 * @file format.h
 * @brief 提供界面显示所需的分到元格式转换。
 */
#pragma once

#include <QString>
#include <QtGlobal>

// 格式化工具：金额（分）→ 元字符串
/**
 * @brief 格式化界面金额，不参与账单计算。
 * @param fen 以分为单位的整数金额。
 * @return 不带货币符号且固定保留两位小数的元字符串。
 */
inline QString fenToYuan(qlonglong fen) {
	return QString::number(fen / 100.0, 'f', 2);
}
