#include "ui/mode_tab_bar.h"

#include <QPainter>
#include <QMouseEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QColor>
#include "ui/theme.h"

namespace zp {
namespace {
constexpr int kTabCount = 2;
constexpr int kUnderlineHeight = 2;
} // namespace

ModeTabBar::ModeTabBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ModeTabBar"));
    setFixedHeight(42);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    // Custom-painted: repaint on theme changes so tab text never stays dark
    // on the light background (or vice versa).
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, static_cast<void (QWidget::*)()>(&QWidget::update));
}

QRect ModeTabBar::tabRect(int index) const
{
    const int w = width() / kTabCount;
    return QRect(index * w, 0, w, height());
}

QString ModeTabBar::tabText(int index) const
{
    return index == 0 ? tr("实时预览") : tr("录像回放");
}

void ModeTabBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= kTabCount || index == m_current) {
        return;
    }
    m_current = index;
    update();
    emit currentModeChanged(index);
}

void ModeTabBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const ThemeTokens &tk = ThemeManager::instance().tokens();
    const bool dark = ThemeManager::instance().presetName() == QLatin1String("dark");
    // 夜晚沿用一个亮一档的 hover 色；白天则向主题蓝靠拢。
    const QColor activeText = dark ? QColor(0xE6, 0xED, 0xF3) : tk.text;
    const QColor hoverText = dark ? QColor(0xC3, 0xCE, 0xDA) : tk.text2;

    for (int i = 0; i < kTabCount; ++i) {
        const QRect r = tabRect(i);
        const bool active = (i == m_current);
        const bool hover = (i == m_hover) && !active;

        // 一级 Tab 视觉更弱：普通文字，hover 略微提亮，不做按钮底色。
        QColor col = active ? activeText : tk.text2;
        if (hover) {
            col = hoverText;
        }
        p.setPen(col);
        QFont f = font();
        f.setPixelSize(13);
        f.setBold(active);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, tabText(i));

        // 底部高亮下划线：当前项用主题蓝，其他项几乎不可见的细线。
        const QRect line(r.left() + r.width() / 4,
                         height() - kUnderlineHeight,
                         r.width() / 2, kUnderlineHeight);
        p.fillRect(line, active ? tk.accent : QColor(tk.accent.red(), tk.accent.green(), tk.accent.blue(), 0));
    }
}

void ModeTabBar::mouseMoveEvent(QMouseEvent *event)
{
    const int index = event->pos().x() / (width() / kTabCount);
    const int hover = (index >= 0 && index < kTabCount) ? index : -1;
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void ModeTabBar::leaveEvent(QEvent *)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

void ModeTabBar::mousePressEvent(QMouseEvent *event)
{
    const int index = event->pos().x() / (width() / kTabCount);
    if (index >= 0 && index < kTabCount) {
        setCurrentIndex(index);
    }
}

} // namespace zp
