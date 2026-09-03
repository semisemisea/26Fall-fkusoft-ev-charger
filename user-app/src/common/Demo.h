#pragma once

#include <QtGlobal>

namespace demo {
inline bool slow()
{
    static const bool value = qEnvironmentVariableIntValue("EV_DEMO_SLOW") != 0;
    return value;
}

inline int ms(int base)
{
    return slow() ? base * 4 : base;
}

inline qreal pressScale()
{
    return slow() ? 0.90 : 0.97;
}
} // namespace demo
