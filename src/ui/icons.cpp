#include "ui/icons.h"
#include "ui/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

namespace zp {
namespace icons {

namespace {
QColor c(color::Rgb rgb)
{
    return QColor(rgb.r, rgb.g, rgb.b);
}
} // namespace

QPixmap folderPixmap(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    QPainterPath path;
    path.moveTo(s * 0.10, s * 0.26);
    path.lineTo(s * 0.40, s * 0.26);
    path.lineTo(s * 0.48, s * 0.36);
    path.lineTo(s * 0.90, s * 0.36);
    path.lineTo(s * 0.90, s * 0.80);
    path.lineTo(s * 0.10, s * 0.80);
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    return pm;
}

QPixmap cameraPixmap(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    p.setPen(Qt::NoPen);

    // body
    QColor body = color;
    body.setAlpha(220);
    p.setBrush(body);
    p.drawRoundedRect(QRectF(s * 0.10, s * 0.28, s * 0.58, s * 0.40),
                      s * 0.12, s * 0.12);

    // lens
    QColor lens = c(color::kBgRoot);
    lens.setAlpha(235);
    p.setBrush(lens);
    p.drawEllipse(QPointF(s * 0.30, s * 0.48), s * 0.12, s * 0.12);
    p.setBrush(color);
    p.drawEllipse(QPointF(s * 0.30, s * 0.48), s * 0.05, s * 0.05);

    // viewfinder wedge
    QPainterPath tri;
    tri.moveTo(s * 0.70, s * 0.38);
    tri.lineTo(s * 0.90, s * 0.30);
    tri.lineTo(s * 0.90, s * 0.66);
    tri.lineTo(s * 0.70, s * 0.58);
    tri.closeSubpath();
    p.setBrush(body);
    p.drawPath(tri);

    return pm;
}

QIcon folder(bool online)
{
    const QColor col = online ? c(color::kAccent2) : c(color::kTextSecond);
    QIcon icon;
    icon.addPixmap(folderPixmap(col, 16), QIcon::Normal);
    icon.addPixmap(folderPixmap(col, 20), QIcon::Normal);
    return icon;
}

QIcon camera(bool online)
{
    const QColor col = online ? c(color::kSuccess) : c(color::kTextSecond);
    QIcon icon;
    icon.addPixmap(cameraPixmap(col, 16), QIcon::Normal);
    icon.addPixmap(cameraPixmap(col, 20), QIcon::Normal);
    return icon;
}

QPixmap appLogo(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    QLinearGradient g(0, 0, s, s);
    g.setColorAt(0, c(color::kAccent));
    g.setColorAt(1, c(color::kAccent2));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.26, s * 0.26);

    // white aperture/camera glyph
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(QRectF(s * 0.20, s * 0.32, s * 0.44, s * 0.34),
                      s * 0.08, s * 0.08);
    QPainterPath tri;
    tri.moveTo(s * 0.66, s * 0.40);
    tri.lineTo(s * 0.82, s * 0.34);
    tri.lineTo(s * 0.82, s * 0.64);
    tri.lineTo(s * 0.66, s * 0.58);
    tri.closeSubpath();
    p.drawPath(tri);
    p.setBrush(c(color::kAccent));
    p.drawEllipse(QPointF(s * 0.36, s * 0.49), s * 0.06, s * 0.06);

    return pm;
}

} // namespace icons
} // namespace zp
