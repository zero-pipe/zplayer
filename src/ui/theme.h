#ifndef ZP_THEME_H
#define ZP_THEME_H

#include <QString>
#include <QColor>
#include <QObject>

class QApplication;

namespace zp {

// All visual parameters as plain data so they can be swapped at runtime and
// persisted. Two built-in presets: "light" (default, aligned with the web
// platform's blue/white) and "dark".
struct ThemeTokens {
    QString name;                 // "light" | "dark"

    QColor bgRoot, bgPanel, bgElevated, bgInput, bgHover;
    QColor border, borderStrong, divider;
    QColor text, text2, muted;

    QColor accent, accent2, accentSoft, accentSoft2;
    QColor success, successSoft;
    QColor warning, warningSoft;
    QColor danger, dangerSoft;

    QColor videoBg;

    int radiusPanel = 12;
    int radiusCard = 10;
    int radiusBtn = 9;
    QString fontFamily = QStringLiteral("\"Segoe UI\", \"Microsoft YaHei UI\", \"PingFang SC\", sans-serif");
    QString monoFamily = QStringLiteral("\"Cascadia Mono\", Consolas, monospace");

    static ThemeTokens light();
    static ThemeTokens dark();
    static ThemeTokens preset(const QString &name);
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager &instance();

    // Applies Fusion + palette + generated stylesheet. Call once at startup.
    void apply(QApplication &app);

    const ThemeTokens &tokens() const { return m_tokens; }
    QString presetName() const { return m_tokens.name; }

    // Switch to a built-in preset ("light"/"dark") and restyle live.
    void setPreset(const QString &name);

signals:
    void themeChanged();

private:
    ThemeManager() = default;
    void restyle();
    QString buildStyleSheet() const;

    QApplication *m_app = nullptr;
    ThemeTokens m_tokens = ThemeTokens::light();
};

} // namespace zp

#endif // ZP_THEME_H
