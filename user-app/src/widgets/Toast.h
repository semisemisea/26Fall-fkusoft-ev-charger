#pragma once

#include <QString>

class QWidget;

namespace Toast {

void success(QWidget *anchor, const QString &text);
void error(QWidget *anchor, const QString &text);
void info(QWidget *anchor, const QString &text);

}
