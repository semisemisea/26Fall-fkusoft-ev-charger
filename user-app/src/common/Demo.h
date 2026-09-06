#pragma once

#include <QtGlobal>

// 演示辅助：EV_DEMO_SLOW=1 时动效放慢 4 倍、按压放大，便于课堂讲解
namespace demo {
// 是否处于慢速演示模式（进程启动时读取一次环境变量）
inline bool slow()
{
    static const bool value = qEnvironmentVariableIntValue("EV_DEMO_SLOW") != 0;
    return value;
}

// 动效时长换算：演示模式放慢 4 倍
inline int ms(int base)
{
    return slow() ? base * 4 : base;
}

// 按钮按压缩放幅度：演示模式更夸张
inline qreal pressScale()
{
    return slow() ? 0.90 : 0.97;
}
} // namespace demo
