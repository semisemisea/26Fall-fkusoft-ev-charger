#pragma once

#include <QColor>
#include <QString>

// 设计令牌：界面颜色一律引用此处，禁止硬编码（规范见 DESIGN.md）
namespace theme {

	// 主色系（品牌绿）：常规/深色/按压/亮调/浅底/浅边框
	inline QColor primary()
	{
		return QColor(0x2B, 0xFF, 0x7D);
	}
	inline QColor primaryDeep()
	{
		return QColor(0x1A, 0x9E, 0x6E);
	}
	inline QColor primaryPress()
	{
		return QColor(0x14, 0x7D, 0x59);
	}
	inline QColor primarySoft()
	{
		return QColor(0x3D, 0xD6, 0x9A);
	}
	inline QColor primaryBg()
	{
		return QColor(0xEA, 0xF8, 0xF0);
	}
	inline QColor primaryBorder()
	{
		return QColor(0xB8, 0xE6, 0xCC);
	}
	// 第二主色（深一档荧光绿）：用于卡片背景、强调按钮
	inline QColor secondary()
	{
		return QColor(0x1A, 0xD6, 0x00);
	}
	// 状态色 - 成功：常规/激活/文字/浅底
	inline QColor success()
	{
		return QColor(0x10, 0xb9, 0x81);
	}
	inline QColor successActive()
	{
		return QColor(0x05, 0x96, 0x69);
	}
	inline QColor successInk()
	{
		return QColor(0x04, 0x78, 0x57);
	}
	inline QColor successBg()
	{
		return QColor(0xec, 0xfd, 0xf5);
	}
	// 状态色 - 警告：常规/文字/浅底
	inline QColor warning()
	{
		return QColor(0xf5, 0x9e, 0x0b);
	}
	inline QColor warningInk()
	{
		return QColor(0x92, 0x40, 0x0e);
	}
	inline QColor warningBg()
	{
		return QColor(0xfe, 0xf3, 0xc7);
	}
	// 状态色 - 错误：常规/强调/深色/浅底
	inline QColor error()
	{
		return QColor(0xf4, 0x3f, 0x5e);
	}
	inline QColor errorStrong()
	{
		return QColor(0xe1, 0x1d, 0x48);
	}
	inline QColor errorDeep()
	{
		return QColor(0xbe, 0x12, 0x3c);
	}
	inline QColor errorBg()
	{
		return QColor(0xff, 0xf1, 0xf2);
	}
	// 文本色：主/次/禁用，以及中性灰
	inline QColor textPrimary()
	{
		return QColor(0x1e, 0x29, 0x3b);
	}
	inline QColor textSecondary()
	{
		return QColor(0x64, 0x74, 0x8d);
	}
	inline QColor textDisabled()
	{
		return QColor(0x94, 0xa3, 0xb8);
	}
	inline QColor neutralGray()
	{
		return QColor(0x47, 0x55, 0x69);
	}
	// 边框、分割线、悬停填充、页面背景、阴影（带透明度）
	inline QColor border()
	{
		return QColor(0xcb, 0xd5, 0xe1);
	}
	inline QColor split()
	{
		return QColor(0xe2, 0xe8, 0xf0);
	}
	inline QColor fillHover()
	{
		return QColor(0xf1, 0xf5, 0xf9);
	}
	inline QColor bgLayout()
	{
		return QColor(0xf8, 0xfa, 0xfc);
	}
	inline QColor shadowInk()
	{
		return QColor(15, 23, 42, 40);
	}

	// 上述颜色的十六进制字符串形式，便于拼接 qss
	inline QString primaryName()
	{
		return primary().name();
	}
	inline QString secondaryName()
	{
		return secondary().name();
	}
	inline QString primaryDeepName()
	{
		return primaryDeep().name();
	}
	inline QString successInkName()
	{
		return successInk().name();
	}
	inline QString warningInkName()
	{
		return warningInk().name();
	}
	inline QString errorName()
	{
		return error().name();
	}
	inline QString errorStrongName()
	{
		return errorStrong().name();
	}
	inline QString textSecondaryName()
	{
		return textSecondary().name();
	}

}
