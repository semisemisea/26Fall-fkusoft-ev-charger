#pragma once

#include <QColor>
#include <QPixmap>

namespace AppIcons {
QPixmap pin(const QColor &color, int size = 24, bool badge = false);
QPixmap bolt(const QColor &color, int size = 24, bool badge = false);
QPixmap person(const QColor &color, int size = 24, bool badge = false);
QPixmap search(const QColor &color, int size = 24, bool badge = false);
} // namespace AppIcons
