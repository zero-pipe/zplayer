#include "ui/pill_segment.h"

#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>

namespace zp {
namespace {
constexpr int kCount = 2;
constexpr int kRadius = 8;      // 轨道圆角
constexpr int kInset = 3;       // 滑块与轨道的内边距
} // namespace

PillSegment::PillSegment(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PillSegment"));
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);

    m_anim = new QPropertyAnimation(this, QByteArrayLiteral("thumbX"), this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

qreal PillSegment::thumbX() const
{
    return m_thumbX;
}

void PillSegment::setThumbX(qreal x)
{
    if (qFuzzyCompare(m_thumbX, x)) {
        return;
    }
    m_thumbX = x;
    update();
}

void PillSegment::setLabels(const QString &first, const QString &second)
{
    m_labels[0] = first;
    m_labels[1] = second;
    update();
}

void PillSegment::setCurrentIndex(int index, bool animated)
{
    if (index < 0 || index >= kCount) {
        return;
    }
    const bool changed = (index != m_current);
    m_current = index;
    updateThumb(animated && changed);
    if (changed) {
        emit currentIndexChanged(index);
        emit currentGroupChanged(index);
    }
}

void PillSegment::updateThumb(bool animated)
{
    const qreal target = kInset + m_current * (width() - 2 * kInset) / kCount;
    if (!animated) {
        m_anim->stop();
        setThumbX(target);
        return;
    }
    if (m_anim->state() == QPropertyAnimation::Running) {
        m_anim->stop();
    }
    m_anim->setStartValue(m_thumbX);
    m_anim->setEndValue(target);
    m_anim->start();
}

void PillSegment::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateThumb(false);
}

int PillSegment::indexAtPosition(int x) const
{
    return (x < width() / kCount) ? 0 : 1;
}

void PillSegment::mousePressEvent(QMouseEvent *event)
{
    setCurrentIndex(indexAtPosition(event->pos().x()));
}

void PillSegment::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 深色圆角轨道（grooveBg / skeletonBorder，来自原型）。
    const QColor track(0x11, 0x16, 0x1F);
    const QColor trackBorder(0x23, 0x2B, 0x3A);
    p.setBrush(track);
    p.setPen(QPen(trackBorder, 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kRadius, kRadius);

    // 高亮药丸滑块（主题蓝 #2B7FFF）。
    const qreal segW = (width() - 2.0 * kInset) / kCount;
    const QRectF thumb(m_thumbX, kInset, segW, height() - 2.0 * kInset);
    p.setBrush(QColor(0x2B, 0x7F, 0xFF));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(thumb, kRadius - kInset, kRadius - kInset);

    // 文字：当前项白，非当前项灰。
    for (int i = 0; i < kCount; ++i) {
        const QRect r(int(i * width() / kCount), 0, int(width() / kCount), height());
        p.setPen(i == m_current ? QColor(0xFF, 0xFF, 0xFF)
                                : QColor(0x9A, 0xA7, 0xB6));
        QFont f = font();
        f.setPixelSize(11);
        f.setBold(i == m_current);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, m_labels[i]);
    }
}

} // namespace zp
