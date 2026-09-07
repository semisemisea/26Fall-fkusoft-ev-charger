#pragma once

#include <QColor>
#include <QPixmap>

// QPainter 自绘图标集：不使用 emoji，避免目标机缺彩色字体时退化为方框
// badge 为 true 时在右上角叠加红点角标；size 为输出像素尺寸（内部按 24x24 基准缩放）
namespace AppIcons {
// 定位图钉（电站位置）
QPixmap pin(const QColor &color, int size = 24, bool badge = false);
// 闪电（充电）
QPixmap bolt(const QColor &color, int size = 24, bool badge = false);
// 人形（用户/我的）
QPixmap person(const QColor &color, int size = 24, bool badge = false);
// 放大镜（搜索）
QPixmap search(const QColor &color, int size = 24, bool badge = false);
// 时钟（预约时段）
QPixmap clock(const QColor &color, int size = 24, bool badge = false);
// 日历（预约日期）
QPixmap calendar(const QColor &color, int size = 24, bool badge = false);
// 钱包（余额/充值）
QPixmap wallet(const QColor &color, int size = 24, bool badge = false);
// 车辆（爱车信息）
QPixmap car(const QColor &color, int size = 24, bool badge = false);
// 信息提示
QPixmap info(const QColor &color, int size = 24, bool badge = false);
// 右向箭头（列表入口）
QPixmap chevronRight(const QColor &color, int size = 24, bool badge = false);
// 右转箭头（导航）
QPixmap turnRight(const QColor &color, int size = 24, bool badge = false);
// 信号强度
QPixmap signal(const QColor &color, int size = 24, bool badge = false);
QPixmap battery(int size = 24, bool badge = false);
QPixmap avatar(const QColor &color, int size = 24, bool badge = false);
// 支付成功：渐变圆圈（主色→黄绿色）+ 主色打勾，自定义宽高
QPixmap successCheck(int width, int height);
} // namespace AppIcons
