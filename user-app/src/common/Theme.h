#pragma once

#include <QColor>
#include <QString>

namespace theme {

inline QColor primary()
{
    return QColor(0x16, 0x77, 0xff);
}
inline QColor primaryHover()
{
    return QColor(0x40, 0x96, 0xff);
}
inline QColor primaryActive()
{
    return QColor(0x09, 0x58, 0xd9);
}
inline QColor primaryBg()
{
    return QColor(0xe6, 0xf4, 0xff);
}
inline QColor success()
{
    return QColor(0x52, 0xc4, 0x1a);
}
inline QColor warning()
{
    return QColor(0xfa, 0xad, 0x14);
}
inline QColor error()
{
    return QColor(0xff, 0x4d, 0x4f);
}
inline QColor textPrimary()
{
    return QColor(0, 0, 0, 224);
}
inline QColor textSecondary()
{
    return QColor(0, 0, 0, 115);
}
inline QColor textDisabled()
{
    return QColor(0, 0, 0, 64);
}
inline QColor border()
{
    return QColor(0xd9, 0xd9, 0xd9);
}
inline QColor split()
{
    return QColor(0xf0, 0xf0, 0xf0);
}
inline QColor fillHover()
{
    return QColor(0, 0, 0, 10);
}
inline QColor bgLayout()
{
    return QColor(0xf5, 0xf5, 0xf5);
}
inline QColor neutralGray()
{
    return QColor(0xbf, 0xbf, 0xbf);
}

inline QString primaryName()
{
    return primary().name();
}
inline QString successName()
{
    return success().name();
}
inline QString warningName()
{
    return warning().name();
}
inline QString errorName()
{
    return error().name();
}
inline QString textSecondaryName()
{
    return QStringLiteral("#8c8c8c");
}

}
