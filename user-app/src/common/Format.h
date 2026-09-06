#pragma once

#include <QString>
#include <QtGlobal>

// 格式化工具：金额（分）→ 元字符串
inline QString fenToYuan(qlonglong fen)
{
    return QString::number(fen / 100.0, 'f', 2);
}
