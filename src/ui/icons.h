#ifndef ZP_ICONS_H
#define ZP_ICONS_H

#include <QIcon>
#include <QPixmap>
#include <QColor>

namespace zp {
namespace icons {

// Painted, theme-colored vector icons (no image assets required).
QIcon folder(bool online = true);
QIcon camera(bool online = true);
QPixmap folderPixmap(const QColor &color, int size);
QPixmap cameraPixmap(const QColor &color, int size);

// Product logo mark: hexagon "smart IoT engine" in a rounded gradient tile.
QPixmap appLogo(int size);

// Stroked line icons (24x24 grid), theme-colored.
enum Glyph {
    User, Lock, Eye, Gear, Search, Grid1, Grid4, Grid9,
    Close, Expand, Refresh, ChevronDown, ArrowRight
};
QPixmap glyph(Glyph g, const QColor &color, int size = 18);

// The large hexagon hero logo for the login brand panel.
QPixmap brandHero(int size);

} // namespace icons
} // namespace zp

#endif // ZP_ICONS_H
