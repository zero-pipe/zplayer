#include "ui/mode_tab_bar.h"

#include <QPainter>
#include <QMouseEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QColor>

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

    const QColor text2(0x9A, 0xA7, 0xB6);
    const QColor accent(0x2B, 0x7F, 0xFF);

    for (int i = 0; i < kTabCount; ++i) {
        const QRect r = tabRect(i);
        const bool active = (i == m_current);
        const bool hover = (i == m_hover) && !active;

        // 一级 Tab 视觉更弱：普通文字，hover 略微提亮，不做按钮底色。
        QColor col = active ? QColor(0xE6, 0xED, 0xF3) : text2;
        if (hover) {
            col = QColor(0xC3, 0xCE, 0xDA);
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
        p.fillRect(line, active ? accent : QColor(0x2B, 0x7F, 0xFF, 0));
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
