#ifndef ZP_THEME_H
#define ZP_THEME_H

class QApplication;

namespace zp {

// Applies the "cold-blue command center" theme: Fusion style, dark palette and
// the global stylesheet (:/theme.qss). Call once at startup.
void applyTheme(QApplication &app);

// Access to the design tokens used by painted widgets (icons/overlays).
namespace color {
struct Rgb { int r, g, b; };
constexpr Rgb kBgRoot      {0x0B, 0x0F, 0x14};
constexpr Rgb kBgPanel     {0x12, 0x18, 0x21};
constexpr Rgb kBgElevated  {0x18, 0x20, 0x2B};
constexpr Rgb kBorder      {0x23, 0x2D, 0x3A};
constexpr Rgb kTextPrimary {0xE6, 0xED, 0xF3};
constexpr Rgb kTextSecond  {0x9A, 0xA7, 0xB6};
constexpr Rgb kAccent      {0x2F, 0x81, 0xF7};
constexpr Rgb kAccent2     {0x22, 0xB8, 0xCF};
constexpr Rgb kSuccess     {0x2E, 0xCC, 0x71};
constexpr Rgb kWarning     {0xE3, 0xB3, 0x41};
constexpr Rgb kDanger      {0xF8, 0x51, 0x49};
} // namespace color

} // namespace zp

#endif // ZP_THEME_H
