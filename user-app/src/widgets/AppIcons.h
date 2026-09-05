#pragma once

#include <QColor>
#include <QPixmap>

namespace AppIcons {
QPixmap pin(const QColor &color, int size = 24, bool badge = false);
QPixmap bolt(const QColor &color, int size = 24, bool badge = false);
QPixmap person(const QColor &color, int size = 24, bool badge = false);
QPixmap search(const QColor &color, int size = 24, bool badge = false);
QPixmap clock(const QColor &color, int size = 24, bool badge = false);
QPixmap calendar(const QColor &color, int size = 24, bool badge = false);
QPixmap wallet(const QColor &color, int size = 24, bool badge = false);
QPixmap car(const QColor &color, int size = 24, bool badge = false);
QPixmap info(const QColor &color, int size = 24, bool badge = false);
QPixmap chevronRight(const QColor &color, int size = 24, bool badge = false);
} // namespace AppIcons
