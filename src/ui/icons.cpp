#include "ui/icons.h"
#include "ui/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace zp {
namespace icons {

namespace {
ThemeTokens tokens() { return ThemeManager::instance().tokens(); }
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
    pm.fill(Qt::transparent);    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    p.setPen(Qt::NoPen);

    QColor body = color;
    body.setAlpha(230);
    p.setBrush(body);
    p.drawRoundedRect(QRectF(s * 0.10, s * 0.28, s * 0.58, s * 0.40), s * 0.12, s * 0.12);

    QColor lens = tokens().bgPanel;
    lens.setAlpha(240);
    p.setBrush(lens);
    p.drawEllipse(QPointF(s * 0.30, s * 0.48), s * 0.12, s * 0.12);
    p.setBrush(color);
    p.drawEllipse(QPointF(s * 0.30, s * 0.48), s * 0.05, s * 0.05);

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

QPixmap playPause(bool playing, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal s = size;
    if (playing) {
        p.drawRoundedRect(QRectF(s * 0.30, s * 0.22, s * 0.14, s * 0.56), 1.5, 1.5);
        p.drawRoundedRect(QRectF(s * 0.56, s * 0.22, s * 0.14, s * 0.56), 1.5, 1.5);
    } else {
        QPainterPath tri;
        tri.moveTo(s * 0.32, s * 0.20);
        tri.lineTo(s * 0.82, s * 0.50);
        tri.lineTo(s * 0.32, s * 0.80);
        tri.closeSubpath();
        p.drawPath(tri);
    }
    return pm;
}

QIcon folder(bool online)
{
    const ThemeTokens t = tokens();
    const QColor col = online ? t.accent : t.text2;
    QIcon icon;
    icon.addPixmap(folderPixmap(col, 16), QIcon::Normal);
    icon.addPixmap(folderPixmap(col, 20), QIcon::Normal);
    return icon;
}

QIcon camera(bool online)
{
    const ThemeTokens t = tokens();
    const QColor col = online ? t.success : t.muted;
    QIcon icon;
    icon.addPixmap(cameraPixmap(col, 16), QIcon::Normal);
    icon.addPixmap(cameraPixmap(col, 20), QIcon::Normal);
    return icon;
}

// Zeroo Player mark: dark rounded tile, a white ring and the red play
// triangle. Drawn from the 32-unit brand grid so every size stays crisp.
QPixmap appLogo(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 32.0, size / 32.0);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x20, 0x21, 0x24));
    p.drawRoundedRect(QRectF(0, 0, 32, 32), 8, 8);

    QPen ring(Qt::white, 2.6);
    p.setPen(ring);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(16, 16), 9.5, 9.5);

    QPainterPath tri;
    tri.moveTo(14.2, 12.6);
    tri.cubicTo(14.2, 12.2, 14.65, 11.95, 15.0, 12.16);
    tri.lineTo(20.2, 15.32);
    tri.cubicTo(20.52, 15.52, 20.52, 15.98, 20.2, 16.18);
    tri.lineTo(15.0, 19.34);
    tri.cubicTo(14.65, 19.55, 14.2, 19.3, 14.2, 18.9);
    tri.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xEA, 0x43, 0x35));
    p.drawPath(tri);

    return pm;
}

namespace {

void strokePath(QPainter &p, const QPainterPath &path, const QColor &col, qreal w)
{
    QPen pen(col, w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

} // namespace

QPixmap glyph(Glyph g, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    const qreal k = s / 24.0;      // scale from a 24x24 design grid
    const qreal w = 1.8 * k;
    p.scale(k, k);

    QPainterPath path;
    switch (g) {
    case User:
        path.addEllipse(QPointF(12, 8), 4, 4);
        path.moveTo(4, 20);
        path.cubicTo(4, 15.5, 8, 14, 12, 14);
        path.cubicTo(16, 14, 20, 15.5, 20, 20);
        break;
    case Lock:
        path.addRoundedRect(QRectF(5, 10, 14, 10), 2, 2);
        path.moveTo(8, 10);
        path.lineTo(8, 7);
        path.cubicTo(8, 4.2, 16, 4.2, 16, 7);
        path.lineTo(16, 10);
        break;
    case Eye:
        path.moveTo(2, 12);
        path.cubicTo(5.5, 5.5, 18.5, 5.5, 22, 12);
        path.cubicTo(18.5, 18.5, 5.5, 18.5, 2, 12);
        path.closeSubpath();
        path.addEllipse(QPointF(12, 12), 3, 3);
        break;
    case Gear: {
        path.addEllipse(QPointF(12, 12), 3, 3);
        QPainterPath ring;
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            const qreal a = i * 2 * M_PI / teeth;
            const QPointF pt(12 + std::cos(a) * 8.5, 12 + std::sin(a) * 8.5);
            ring.addEllipse(pt, 1.6, 1.6);
        }
        path.addPath(ring);
        path.addEllipse(QPointF(12, 12), 6, 6);
        break;
    }
    case Search:
        path.addEllipse(QPointF(11, 11), 7, 7);
        path.moveTo(16, 16);
        path.lineTo(21, 21);
        break;
    case Grid1:
        path.addRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        break;
    case Grid4:
        path.addRoundedRect(QRectF(4, 4, 7, 7), 1, 1);
        path.addRoundedRect(QRectF(13, 4, 7, 7), 1, 1);
        path.addRoundedRect(QRectF(4, 13, 7, 7), 1, 1);
        path.addRoundedRect(QRectF(13, 13, 7, 7), 1, 1);
        break;
    case Grid9:
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                path.addRect(QRectF(4 + c * 6, 4 + r * 6, 4, 4));
        break;
    case Close:
        path.moveTo(6, 6); path.lineTo(18, 18);
        path.moveTo(18, 6); path.lineTo(6, 18);
        break;
    case Expand:
        path.moveTo(4, 9); path.lineTo(4, 4); path.lineTo(9, 4);
        path.moveTo(20, 9); path.lineTo(20, 4); path.lineTo(15, 4);
        path.moveTo(4, 15); path.lineTo(4, 20); path.lineTo(9, 20);
        path.moveTo(20, 15); path.lineTo(20, 20); path.lineTo(15, 20);
        break;
    case Refresh:
        path.arcMoveTo(QRectF(3, 3, 18, 18), 60);
        path.arcTo(QRectF(3, 3, 18, 18), 60, 300);
        path.moveTo(18.5, 3);
        path.lineTo(19, 7.5);
        path.lineTo(14.5, 7);
        break;
    case ChevronDown:
        path.moveTo(6, 9); path.lineTo(12, 15); path.lineTo(18, 9);
        break;
    case ArrowRight:
        path.moveTo(5, 12); path.lineTo(19, 12);
        path.moveTo(13, 6); path.lineTo(19, 12); path.lineTo(13, 18);
        break;
    case Sun: {
        path.addEllipse(QPointF(12, 12), 4, 4);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4.0;
            path.moveTo(12 + std::cos(a) * 7.0, 12 + std::sin(a) * 7.0);
            path.lineTo(12 + std::cos(a) * 9.5, 12 + std::sin(a) * 9.5);
        }
        break;
    }
    case Moon:
        path.moveTo(14.5, 3.5);
        path.cubicTo(8.0, 4.5, 6.0, 11.0, 8.5, 15.5);
        path.cubicTo(10.5, 19.0, 15.0, 19.5, 18.5, 17.0);
        path.cubicTo(12.5, 17.5, 9.5, 11.5, 14.5, 3.5);
        break;
    case WinMin:
        path.moveTo(7, 12);
        path.lineTo(17, 12);
        break;
    case WinMax:
        path.addRoundedRect(QRectF(7, 7, 10, 10), 1.5, 1.5);
        break;
    case WinRestore: {
        // Back window (partial outline) + front window.
        path.moveTo(9, 7);
        path.lineTo(17, 7);
        path.lineTo(17, 15);
        path.addRoundedRect(QRectF(7, 9, 8, 8), 1.5, 1.5);
        break;
    }
    case AiScan: {
        // Scan corners around a sensor dot: "AI is watching this frame".
        path.moveTo(4, 9);  path.lineTo(4, 4);  path.lineTo(9, 4);
        path.moveTo(15, 4); path.lineTo(20, 4); path.lineTo(20, 9);
        path.moveTo(20, 15); path.lineTo(20, 20); path.lineTo(15, 20);
        path.moveTo(9, 20); path.lineTo(4, 20); path.lineTo(4, 15);
        path.addEllipse(QPointF(12, 12), 2.2, 2.2);
        break;
    }
    case FastSkip:
        path.moveTo(5, 7);  path.lineTo(11, 12); path.lineTo(5, 17);
        path.moveTo(12, 7); path.lineTo(18, 12); path.lineTo(12, 17);
        break;
    case Ocr:
        // "T" over a baseline with scan brackets.
        path.moveTo(8, 8);  path.lineTo(16, 8);
        path.moveTo(12, 8); path.lineTo(12, 15);
        path.moveTo(8, 18); path.lineTo(16, 18);
        path.moveTo(4, 5);  path.lineTo(4, 3);  path.lineTo(6, 3);
        path.moveTo(18, 3); path.lineTo(20, 3); path.lineTo(20, 5);
        break;
    case Roi:
        // Frame with corner handles and an inner selection box.
        path.moveTo(3, 7);  path.lineTo(3, 3);  path.lineTo(7, 3);
        path.moveTo(17, 3); path.lineTo(21, 3); path.lineTo(21, 7);
        path.moveTo(21, 17); path.lineTo(21, 21); path.lineTo(17, 21);
        path.moveTo(7, 21); path.lineTo(3, 21); path.lineTo(3, 17);
        path.addRect(QRectF(8, 8, 8, 8));
        break;
    case Magnify:
        path.addEllipse(QPointF(10.5, 10.5), 6, 6);
        path.moveTo(15, 15);  path.lineTo(20.5, 20.5);
        path.moveTo(10.5, 7.8); path.lineTo(10.5, 13.2);
        path.moveTo(7.8, 10.5); path.lineTo(13.2, 10.5);
        break;
    case Sparkle:
        path.moveTo(12, 2.5);
        path.lineTo(13.7, 10.3);
        path.lineTo(21.5, 12);
        path.lineTo(13.7, 13.7);
        path.lineTo(12, 21.5);
        path.lineTo(10.3, 13.7);
        path.lineTo(2.5, 12);
        path.lineTo(10.3, 10.3);
        path.closeSubpath();
        break;
    case Wave:
        // Speech waveform crossed out: noise removed.
        path.moveTo(3, 14);
        path.cubicTo(5, 9, 7, 19, 9, 14);
        path.cubicTo(11, 9, 13, 19, 15, 14);
        path.cubicTo(16.2, 11.6, 17.4, 13.4, 18, 13.4);
        path.moveTo(7, 18.5); path.lineTo(18.5, 7);
        break;
    case SearchClock:
        path.addEllipse(QPointF(10.5, 10.5), 6, 6);
        path.moveTo(15, 15); path.lineTo(20.5, 20.5);
        path.moveTo(10.5, 10.5); path.lineTo(10.5, 7.2);
        path.moveTo(10.5, 10.5); path.lineTo(13.2, 11.8);
        break;
    case Export:
        path.moveTo(12, 3.5);  path.lineTo(12, 14);
        path.moveTo(8, 10);    path.lineTo(12, 14); path.lineTo(16, 10);
        path.moveTo(4.5, 16);  path.lineTo(4.5, 19.5);
        path.lineTo(19.5, 19.5); path.lineTo(19.5, 16);
        break;
    case Snapshot:
        // Line camera body + lens + top viewfinder bump, matched to the rest
        // of the 16 px bar icon set.
        path.addRoundedRect(QRectF(3, 7, 18, 12.5), 2, 2);
        path.moveTo(8.5, 7);  path.lineTo(9.5, 4.5);
        path.lineTo(14.5, 4.5); path.lineTo(15.5, 7);
        path.addEllipse(QPointF(12, 13), 3.2, 3.2);
        break;
    case Layout1:
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2.5, 2.5);
        break;
    case Layout2x2:
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        path.moveTo(12, 3);  path.lineTo(12, 21);
        path.moveTo(3, 12);  path.lineTo(21, 12);
        break;
    case Layout3x3:
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        path.moveTo(9, 3);   path.lineTo(9, 21);
        path.moveTo(15, 3);  path.lineTo(15, 21);
        path.moveTo(3, 9);   path.lineTo(21, 9);
        path.moveTo(3, 15);  path.lineTo(21, 15);
        break;
    case Layout4x4:
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        for (int i = 1; i < 4; ++i) {
            const qreal d = 3.0 + 18.0 * i / 4.0;
            path.moveTo(d, 3);  path.lineTo(d, 21);
            path.moveTo(3, d);  path.lineTo(21, d);
        }
        break;
    case Layout1p5:
        // Big pane = top-left 2x2 of a 3x3; two small panes down the right
        // column; three small panes across the bottom row.
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        path.moveTo(15, 3);  path.lineTo(15, 21);   // big | right column
        path.moveTo(3, 15);  path.lineTo(21, 15);   // big/right | bottom row
        path.moveTo(9, 15);  path.lineTo(9, 21);    // bottom row column split
        path.moveTo(15, 9);  path.lineTo(21, 9);    // right column split
        break;
    }
    strokePath(p, path, color, w);
    return pm;
}

QPixmap ptzArrow(int direction, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;
    // Rounded chevron pointing up in a 24-unit design space, rotated to the
    // requested direction (0 = up, clockwise). Chevrons read uniformly and stay
    // soft/rounded for all 8 directions, unlike solid triangles.
    p.translate(s * 0.5, s * 0.5);
    p.rotate(direction * 45.0);
    const qreal k = s / 24.0;
    p.scale(k, k);

    QPainterPath chevron;
    chevron.moveTo(-5.0, 1.6);
    chevron.lineTo(0.0, -3.6);
    chevron.lineTo(5.0, 1.6);

    QPen pen(color, 2.6);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(chevron);
    return pm;
}

QPixmap ptzStop(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal s = size;
    QRectF r(s * 0.30, s * 0.30, s * 0.40, s * 0.40);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(r, s * 0.08, s * 0.08);
    return pm;
}

QPixmap ptzZoom(bool zoomIn, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal s = size;

    // Soft disc background.
    p.setPen(Qt::NoPen);
    QColor disc = color;
    disc.setAlpha(28);
    p.setBrush(disc);
    p.drawEllipse(QRectF(0, 0, s, s));

    // Plus / minus strokes, centered.
    QPen pen(color, s * 0.11);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const qreal cx = s * 0.5, cy = s * 0.5, arm = s * 0.20;
    p.drawLine(QPointF(cx - arm, cy), QPointF(cx + arm, cy));
    if (zoomIn) {
        p.drawLine(QPointF(cx, cy - arm), QPointF(cx, cy + arm));
    }
    return pm;
}

QPixmap chevron(bool right, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal s = size;
    p.translate(s * 0.5, s * 0.5);
    if (!right) {
        p.rotate(180.0);
    }
    p.scale(s / 24.0, s / 24.0);

    QPainterPath path;
    path.moveTo(-3.5, -6.5);
    path.lineTo(3.5, 0.0);
    path.lineTo(-3.5, 6.5);

    QPen pen(color, 2.6);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    return pm;
}

QPixmap brandHero(int size)
{
    // The login brand panel reuses the app mark for one consistent identity.
    return appLogo(size);
}

} // namespace icons
} // namespace zp
