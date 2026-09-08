/**
 * @file AppIcons.h
 * @brief 使用 QPainter 生成不依赖外部字体的矢量风格位图图标。
 */
#pragma once

#include <QColor>
#include <QPixmap>

// QPainter 自绘图标集：不使用 emoji，避免目标机缺彩色字体时退化为方框
// badge 为 true 时在右上角叠加红点角标；size 为输出像素尺寸（内部按 24x24 基准缩放）
namespace AppIcons {
	/**
	 * @brief 定位图钉（电站位置）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap pin(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 闪电（充电）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap bolt(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 人形（用户/我的）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap person(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 放大镜（搜索）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap search(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 时钟（预约时段）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap clock(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 日历（预约日期）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap calendar(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 钱包（余额/充值）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap wallet(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 车辆（爱车信息）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap car(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 信息提示
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap info(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 右向箭头（列表入口）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap chevronRight(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 右转箭头（导航）
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap turnRight(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 信号强度
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap signal(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 绘制用于状态栏的横向电池示意图。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap battery(int size = 24, bool badge = false);
	/**
	 * @brief 按给定百分比和波浪相位绘制竖电池。
	 * @param size 相对于 32 像素基准的尺寸参数；输出宽高按 17.10:34 缩放。
	 * @param percent 归一化电量比例；限制在 0 到 1，显示文字时再乘 100。
	 * @param wavePhase 波浪相位，以弧度参与正弦运算。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap batteryVertical(int size = 24, double percent = 0.0, double wavePhase = 0.0);
	/**
	 * @brief 绘制透明背景的人形头像轮廓。
	 * @param color 图标主体颜色。
	 * @param size 输出方形画布的边长，单位为像素。
	 * @param badge true 在图标右上角叠加红点。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap avatar(const QColor &color, int size = 24, bool badge = false);
	/**
	 * @brief 支付成功：渐变圆圈（主色→黄绿色）+ 主色打勾，自定义宽高
	 * @param width 输出图像宽度，单位为像素。
	 * @param height 输出图像或卡片高度，单位为像素。
	 * @return 按指定尺寸绘制的透明背景 QPixmap 值。
	 */
	QPixmap successCheck(int width, int height);
} // namespace AppIcons
