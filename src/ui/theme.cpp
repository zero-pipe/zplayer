#include "ui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QFile>

namespace zp {

void applyTheme(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));

    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x0B, 0x0F, 0x14));
    pal.setColor(QPalette::WindowText, QColor(0xE6, 0xED, 0xF3));
    pal.setColor(QPalette::Base, QColor(0x0E, 0x14, 0x1B));
    pal.setColor(QPalette::AlternateBase, QColor(0x12, 0x18, 0x21));
    pal.setColor(QPalette::Text, QColor(0xE6, 0xED, 0xF3));
    pal.setColor(QPalette::Button, QColor(0x18, 0x20, 0x2B));
    pal.setColor(QPalette::ButtonText, QColor(0xE6, 0xED, 0xF3));
    pal.setColor(QPalette::Highlight, QColor(0x2F, 0x81, 0xF7));
    pal.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    pal.setColor(QPalette::ToolTipBase, QColor(0x18, 0x20, 0x2B));
    pal.setColor(QPalette::ToolTipText, QColor(0xE6, 0xED, 0xF3));
    pal.setColor(QPalette::PlaceholderText, QColor(0x5F, 0x6B, 0x7A));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5F, 0x6B, 0x7A));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5F, 0x6B, 0x7A));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5F, 0x6B, 0x7A));
    app.setPalette(pal);

    QFile qss(QStringLiteral(":/resources/theme.qss"));
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
}

} // namespace zp
