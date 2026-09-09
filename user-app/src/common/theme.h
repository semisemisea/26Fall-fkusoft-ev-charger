/**
 * @file theme.h
 * @brief 定义用户前端共享的品牌、状态、文本及背景颜色。
 */
#pragma once

#include <QColor>
#include <QString>

// 设计令牌：界面颜色一律引用此处，禁止硬编码（规范见 DESIGN.md）
namespace theme {

	/**
	 * @brief 读取品牌主绿的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primary() {
		return QColor(0x2B, 0xFF, 0x7D);
	}
	/**
	 * @brief 读取品牌深绿的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primaryDeep() {
		return QColor(0x1A, 0x9E, 0x6E);
	}
	/**
	 * @brief 读取品牌按压绿的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primaryPress() {
		return QColor(0x14, 0x7D, 0x59);
	}
	/**
	 * @brief 读取品牌柔和绿的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primarySoft() {
		return QColor(0x3D, 0xD6, 0x9A);
	}
	/**
	 * @brief 读取品牌浅背景的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primaryBg() {
		return QColor(0xEA, 0xF8, 0xF0);
	}
	/**
	 * @brief 读取品牌浅边框的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor primaryBorder() {
		return QColor(0xB8, 0xE6, 0xCC);
	}
	/**
	 * @brief 读取第二主色的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor secondary() {
		return QColor(0x1A, 0xD6, 0x00);
	}
	/**
	 * @brief 读取成功的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor success() {
		return QColor(0x10, 0xb9, 0x81);
	}
	/**
	 * @brief 读取成功激活态的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor successActive() {
		return QColor(0x05, 0x96, 0x69);
	}
	/**
	 * @brief 读取成功文字的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor successInk() {
		return QColor(0x04, 0x78, 0x57);
	}
	/**
	 * @brief 读取成功浅背景的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor successBg() {
		return QColor(0xec, 0xfd, 0xf5);
	}
	/**
	 * @brief 读取警告的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor warning() {
		return QColor(0xf5, 0x9e, 0x0b);
	}
	/**
	 * @brief 读取警告文字的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor warningInk() {
		return QColor(0x92, 0x40, 0x0e);
	}
	/**
	 * @brief 读取警告浅背景的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor warningBg() {
		return QColor(0xfe, 0xf3, 0xc7);
	}
	/**
	 * @brief 读取错误的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor error() {
		return QColor(0xf4, 0x3f, 0x5e);
	}
	/**
	 * @brief 读取错误强调的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor errorStrong() {
		return QColor(0xe1, 0x1d, 0x48);
	}
	/**
	 * @brief 读取错误深色的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor errorDeep() {
		return QColor(0xbe, 0x12, 0x3c);
	}
	/**
	 * @brief 读取错误浅背景的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor errorBg() {
		return QColor(0xff, 0xf1, 0xf2);
	}
	/**
	 * @brief 读取主要文本的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor textPrimary() {
		return QColor(0x1e, 0x29, 0x3b);
	}
	/**
	 * @brief 读取次要文本的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor textSecondary() {
		return QColor(0x64, 0x74, 0x8d);
	}
	/**
	 * @brief 读取禁用文本的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor textDisabled() {
		return QColor(0x94, 0xa3, 0xb8);
	}
	/**
	 * @brief 读取中性灰的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor neutralGray() {
		return QColor(0x47, 0x55, 0x69);
	}
	/**
	 * @brief 读取边框的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor border() {
		return QColor(0xcb, 0xd5, 0xe1);
	}
	/**
	 * @brief 读取分割线的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor split() {
		return QColor(0xe2, 0xe8, 0xf0);
	}
	/**
	 * @brief 读取悬停填充的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor fillHover() {
		return QColor(0xf1, 0xf5, 0xf9);
	}
	/**
	 * @brief 读取页面背景的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor bgLayout() {
		return QColor(0xf8, 0xfa, 0xfc);
	}
	/**
	 * @brief 读取半透明阴影的主题颜色。
	 * @return 按值返回的 QColor；调用方可独立修改。
	 */
	inline QColor shadowInk() {
		return QColor(15, 23, 42, 40);
	}

	/**
	 * @brief 读取品牌主绿的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString primaryName() {
		return primary().name();
	}
	/**
	 * @brief 读取第二主色的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString secondaryName() {
		return secondary().name();
	}
	/**
	 * @brief 读取品牌深绿的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString primaryDeepName() {
		return primaryDeep().name();
	}
	/**
	 * @brief 读取成功文字的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString successInkName() {
		return successInk().name();
	}
	/**
	 * @brief 读取警告文字的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString warningInkName() {
		return warningInk().name();
	}
	/**
	 * @brief 读取错误的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString errorName() {
		return error().name();
	}
	/**
	 * @brief 读取错误强调的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString errorStrongName() {
		return errorStrong().name();
	}
	/**
	 * @brief 读取次要文本的十六进制色值文本。
	 * @return 可直接用于样式表的颜色文本。
	 */
	inline QString textSecondaryName() {
		return textSecondary().name();
	}

} // namespace theme
