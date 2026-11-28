#include "ui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QFile>
#include <QMap>

namespace zp {

namespace {

QColor c(const char *hex) { return QColor(QString::fromLatin1(hex)); }

} // namespace

ThemeTokens ThemeTokens::light()
{
    ThemeTokens t;
    t.name = QStringLiteral("light");

    // Zeroo AI product palette (slate + blue): calm neutrals with a single
    // primary blue and semantic status colours (green=ok, amber=warning,
    // rose=bad) used for confidence / state chips.
    t.bgRoot     = c("#F8FAFC");
    t.bgPanel    = c("#FFFFFF");
    t.bgElevated = c("#FFFFFF");
    t.bgInput    = c("#F1F5F9");
    t.bgHover    = c("#EEF2F7");
    t.border       = c("#E2E8F0");
    t.borderStrong = c("#CBD5E1");
    t.divider      = c("#EEF2F6");

    t.text  = c("#1E293B");
    t.text2 = c("#475569");
    t.muted = c("#94A3B8");

    t.accent      = c("#2563EB");
    t.accent2     = c("#1D4ED8");
    t.accentSoft  = QColor(37, 99, 235, 20);
    t.accentSoft2 = QColor(37, 99, 235, 40);

    t.success     = c("#16A34A");
    t.successSoft = QColor(22, 163, 74, 26);
    t.warning     = c("#F59E0B");
    t.warningSoft = QColor(245, 158, 11, 32);
    t.danger      = c("#E11D48");
    t.dangerSoft  = QColor(225, 29, 72, 26);

    t.videoBg = c("#F1F5F9");

    // Custom-painted widgets: pale groove/panel, dark ticks, blue thumb.
    t.segmentedTrack   = c("#E2E8F0");
    t.segmentedThumb   = c("#2B7FFF");
    t.segmentedTextOff = c("#475569");
    t.timelineBg       = c("#F1F5F9");
    t.timelineTrack    = c("#E2E8F0");
    t.timelineTick     = c("#475569");
    t.timelineClip     = c("#2563EB");
    t.timelinePlayhead = c("#EF4444");
    return t;
}

ThemeTokens ThemeTokens::dark()
{
    ThemeTokens t;
    t.name = QStringLiteral("dark");

    // Dark layers (HTML prototype): the center stage is the darkest, the
    // surrounding chrome is one step lighter and cards one step above that.
    // Dividing lines use a single skeleton color so panel edges stay continuous.
    t.bgRoot     = c("#0F131A");   // stageBg: center video stage
    t.bgPanel    = c("#161B24");   // surfaceBg: top / side / bottom chrome
    t.bgElevated = c("#283244");   // cardBorder tone, used for raised surfaces
    t.bgInput    = c("#1E2532");   // cardBg: inputs / cards
    t.bgHover    = c("#283244");
    t.border       = c("#283244"); // cardBorder: inner cards / controls
    t.borderStrong = c("#232B3A"); // skeletonBorder: the one-pixel panel divider
    t.divider      = c("#232B3A"); // same continuous line between regions

    t.text  = c("#E6EDF3");
    t.text2 = c("#9AA7B6");
    t.muted = c("#5F6B7A");

    t.accent      = c("#2B7FFF");
    t.accent2     = c("#60A5FA");
    t.accentSoft  = QColor(43, 127, 255, 34);
    t.accentSoft2 = QColor(43, 127, 255, 62);

    t.success     = c("#22C55E");
    t.successSoft = QColor(34, 197, 94, 30);
    t.warning     = c("#F59E0B");
    t.warningSoft = QColor(245, 158, 11, 30);
    t.danger      = c("#F43F5E");
    t.dangerSoft  = QColor(244, 63, 94, 30);

    t.videoBg = c("#0F131A");

    // Dark prototype palette: preserved exactly as before for night mode.
    t.segmentedTrack   = c("#11161F");
    t.segmentedThumb   = c("#2B7FFF");
    t.segmentedTextOff = c("#9AA7B6");
    t.timelineBg       = c("#11161F");
    t.timelineTrack    = c("#1A2230");
    t.timelineTick     = c("#5C6B82");
    t.timelineClip     = c("#2563EB");
    t.timelinePlayhead = c("#EF4444");
    return t;
}

ThemeTokens ThemeTokens::preset(const QString &name)
{
    if (name.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0) {
        return dark();
    }
    return light();
}

ThemeManager &ThemeManager::instance()
{
    static ThemeManager m;
    return m;
}

void ThemeManager::apply(QApplication &app)
{
    m_app = &app;
    app.setStyle(QStringLiteral("Fusion"));
    restyle();
}

void ThemeManager::setPreset(const QString &name)
{
    m_tokens = ThemeTokens::preset(name);
    restyle();
    emit themeChanged();
}

void ThemeManager::restyle()
{
    if (!m_app) {
        return;
    }

    QPalette pal;
    pal.setColor(QPalette::Window, m_tokens.bgRoot);
    pal.setColor(QPalette::WindowText, m_tokens.text);
    pal.setColor(QPalette::Base, m_tokens.bgInput);
    pal.setColor(QPalette::AlternateBase, m_tokens.bgPanel);
    pal.setColor(QPalette::Text, m_tokens.text);
    pal.setColor(QPalette::Button, m_tokens.bgInput);
    pal.setColor(QPalette::ButtonText, m_tokens.text);
    pal.setColor(QPalette::Highlight, m_tokens.accent);
    pal.setColor(QPalette::HighlightedText, QColor(Qt::white));
    pal.setColor(QPalette::ToolTipBase, m_tokens.bgElevated);
    pal.setColor(QPalette::ToolTipText, m_tokens.text);
    pal.setColor(QPalette::PlaceholderText, m_tokens.muted);
    // Bevel roles: the Fusion style paints tab/frame edges from Light/Mid/Dark.
    // Left at Qt's defaults they stay bright, which shows up as a stray white
    // 1px highlight along tab bars in the dark theme (invisible in light mode).
    pal.setColor(QPalette::Light, m_tokens.bgPanel.lighter(115));
    pal.setColor(QPalette::Midlight, m_tokens.border);
    pal.setColor(QPalette::Mid, m_tokens.border);
    pal.setColor(QPalette::Dark, m_tokens.borderStrong);
    pal.setColor(QPalette::Shadow, m_tokens.bgRoot.darker(130));
    pal.setColor(QPalette::Disabled, QPalette::Text, m_tokens.muted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, m_tokens.muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, m_tokens.muted);
    m_app->setPalette(pal);

    m_app->setStyleSheet(buildStyleSheet());
}

QString ThemeManager::buildStyleSheet() const
{
    QFile f(QStringLiteral(":/resources/theme.qss"));
    if (!f.open(QFile::ReadOnly | QFile::Text)) {
        return QString();
    }
    QString qss = QString::fromUtf8(f.readAll());

    // Template placeholders: %bgRoot%, %accent%, ... replaced by token values.
    QMap<QString, QString> rep;
    auto put = [&rep](const char *key, const QColor &col) {
        rep.insert(QStringLiteral("%%1%").arg(QString::fromLatin1(key)), col.name(QColor::HexArgb));
    };
    put("bgRoot", m_tokens.bgRoot);
    put("bgPanel", m_tokens.bgPanel);
    put("bgElevated", m_tokens.bgElevated);
    put("bgInput", m_tokens.bgInput);
    put("bgHover", m_tokens.bgHover);
    put("border", m_tokens.border);
    put("borderStrong", m_tokens.borderStrong);
    put("divider", m_tokens.divider);
    put("text", m_tokens.text);
    put("text2", m_tokens.text2);
    put("muted", m_tokens.muted);
    put("accent", m_tokens.accent);
    put("accent2", m_tokens.accent2);
    put("accentSoft", m_tokens.accentSoft);
    put("accentSoft2", m_tokens.accentSoft2);
    put("success", m_tokens.success);
    put("successSoft", m_tokens.successSoft);
    put("warning", m_tokens.warning);
    put("warningSoft", m_tokens.warningSoft);
    put("danger", m_tokens.danger);
    put("dangerSoft", m_tokens.dangerSoft);
    put("videoBg", m_tokens.videoBg);
    rep.insert(QStringLiteral("%radiusPanel%"), QString::number(m_tokens.radiusPanel));
    rep.insert(QStringLiteral("%radiusCard%"), QString::number(m_tokens.radiusCard));
    rep.insert(QStringLiteral("%radiusBtn%"), QString::number(m_tokens.radiusBtn));
    rep.insert(QStringLiteral("%fontFamily%"), m_tokens.fontFamily);
    rep.insert(QStringLiteral("%monoFamily%"), m_tokens.monoFamily);

    for (auto it = rep.constBegin(); it != rep.constEnd(); ++it) {
        qss.replace(it.key(), it.value());
    }
    return qss;
}

} // namespace zp
