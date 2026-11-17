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

// Product logo mark (rounded gradient square with a camera glyph).
QPixmap appLogo(int size);

} // namespace icons
} // namespace zp

#endif // ZP_ICONS_H
