#pragma once

#include <QColor>
#include <QString>

namespace theme {

	inline QColor primary()
	{
		return QColor(0x22, 0xC5, 0x86);
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

	inline QString primaryName()
	{
		return primary().name();
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
