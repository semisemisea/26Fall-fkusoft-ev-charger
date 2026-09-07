#pragma once

#include <QString>

class QWidget;

// 顶部轻提示：2.4s 自动消失、不阻塞操作；通知类反馈一律用它，确认类交互仍用对话框
namespace Toast {

// 成功提示（绿点）
void success(QWidget *anchor, const QString &text);
// 错误提示（红点）
void error(QWidget *anchor, const QString &text);
// 一般信息提示（主色点）
void info(QWidget *anchor, const QString &text);

}
