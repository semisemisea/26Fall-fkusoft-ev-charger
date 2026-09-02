#pragma once

#include <QString>
#include <QtGlobal>

inline QString fenToYuan(qlonglong fen)
{
    return QString::number(fen / 100.0, 'f', 2);
}
