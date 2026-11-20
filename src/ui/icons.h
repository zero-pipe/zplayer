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

// Filled play triangle / pause bars for the transport control.
QPixmap playPause(bool playing, const QColor &color, int size = 16);

// Product logo mark: hexagon "smart IoT engine" in a rounded gradient tile.
QPixmap appLogo(int size);

// Stroked line icons (24x24 grid), theme-colored.
enum Glyph {
    User, Lock, Eye, Gear, Search, Grid1, Grid4, Grid9,
    Close, Expand, Refresh, ChevronDown, ArrowRight, Sun, Moon,
    WinMin, WinMax, WinRestore,
    // AI control-bar icon set (16 px line icons, theme-coloured).
    AiScan,      // corner brackets + centre dot: AI 检查
    FastSkip,    // double chevron: 无人快进
    Ocr,         // text lines + scan corners: OCR 识别
    Roi,         // frame with corner handles: ROI 圈选
    Magnify,     // magnifier with plus: 电子放大
    Sparkle,     // four-point star: 画质锐化
    Wave,        // waveform with slash: 语音降噪
    SearchClock, // magnifier with clock: 智能检索
    Export,      // arrow into tray: 导出证据
    Snapshot     // line camera: 抓图
};
QPixmap glyph(Glyph g, const QColor &color, int size = 18);

// PTZ directional arrows: filled rounded triangles pointing in 8 directions
// (0 = up, clockwise). Not font glyphs, so they stay crisp and even-weight.
QPixmap ptzArrow(int direction /*0..7, 0=up clockwise*/, const QColor &color, int size = 20);
// PTZ stop: a rounded square.
QPixmap ptzStop(const QColor &color, int size = 20);
// PTZ zoom: a "+" / "−" centered inside a soft rounded disc.
QPixmap ptzZoom(bool zoomIn, const QColor &color, int size = 20);
// A single rounded chevron (right = ">", left = "<"), used by collapse handles.
QPixmap chevron(bool right, const QColor &color, int size = 16);

// The large hexagon hero logo for the login brand panel.
QPixmap brandHero(int size);

} // namespace icons
} // namespace zp

#endif // ZP_ICONS_H
