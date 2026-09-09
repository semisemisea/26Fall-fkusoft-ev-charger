/**
 * @file toast.h
 * @brief 在锚点窗口顶部显示非阻塞提示，并自动完成入场与退出销毁。
 */
#pragma once

#include <QString>

class QWidget;

// 顶部轻提示：2.4s 自动消失、不阻塞操作；通知类反馈一律用它，确认类交互仍用对话框
namespace Toast {

	/**
	 * @brief 成功提示（绿点）
	 * @param anchor 用于确定提示所属顶层窗口的控件；为空时不显示。
	 * @param text 需要显示的文字。
	 */
	void success(QWidget *anchor, const QString &text);
	/**
	 * @brief 错误提示（红点）
	 * @param anchor 用于确定提示所属顶层窗口的控件；为空时不显示。
	 * @param text 需要显示的文字。
	 */
	void error(QWidget *anchor, const QString &text);
	/**
	 * @brief 一般信息提示（主色点）
	 * @param anchor 用于确定提示所属顶层窗口的控件；为空时不显示。
	 * @param text 需要显示的文字。
	 */
	void info(QWidget *anchor, const QString &text);

} // namespace Toast
