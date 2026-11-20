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
    return t;
}

ThemeTokens ThemeTokens::dark()
{
    ThemeTokens t;
    t.name = QStringLiteral("dark");

    t.bgRoot     = c("#1A212B");
    t.bgPanel    = c("#212A36");
    t.bgElevated = c("#2A3542");
    t.bgInput    = c("#1C242F");
    t.bgHover    = c("#2C3947");
    t.border       = c("#39485A");
    t.borderStrong = c("#4B5D72");
    t.divider      = c("#2B3644");

    t.text  = c("#E6EDF3");
    t.text2 = c("#9AA7B6");
    t.muted = c("#5F6B7A");

    t.accent      = c("#3B82F6");
    t.accent2     = c("#60A5FA");
    t.accentSoft  = QColor(59, 130, 246, 34);
    t.accentSoft2 = QColor(59, 130, 246, 62);

    t.success     = c("#22C55E");
    t.successSoft = QColor(34, 197, 94, 30);
    t.warning     = c("#F59E0B");
    t.warningSoft = QColor(245, 158, 11, 30);
    t.danger      = c("#F43F5E");
    t.dangerSoft  = QColor(244, 63, 94, 30);

    t.videoBg = c("#121A24");
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
