#include "ui/analysis_overlay.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

namespace zp {

namespace {

// Stable, visually distinct color per class id (no dependency on the detector).
QColor colorFor(int classId)
{
    if (classId == 0) {
        return QColor(47, 129, 247);      // person — brand blue
    }
    if (classId < 0) {
        return QColor(120, 120, 120);
    }
    return QColor::fromHsv((classId * 53) % 360, 190, 245);
}

} // namespace

AnalysisOverlay::AnalysisOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("AnalysisOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void AnalysisOverlay::setBoxes(const QVector<AnalysisBox> &boxes,
                               const QRectF &videoRect)
{
    m_boxes = boxes;
    m_videoRect = videoRect.isNull() ? QRectF(0.0, 0.0, 1.0, 1.0) : videoRect;
    update();
}

void AnalysisOverlay::clear()
{
    if (m_boxes.isEmpty()) {
        return;
    }
    m_boxes.clear();
    update();
}

void AnalysisOverlay::setLabelsVisible(bool visible)
{
    m_labels = visible;
    update();
}

void AnalysisOverlay::setRoi(const QRectF &roi)
{
    m_roi = roi.normalized();
    update();
}

void AnalysisOverlay::setRoiDraft(const QRectF &draft)
{
    m_roiDraft = draft.normalized();
    update();
}

void AnalysisOverlay::paintEvent(QPaintEvent *)
{
    if (width() <= 0 || height() <= 0) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal w = width();
    const qreal h = height();

    // ---- ROI frame (dashed; amber while dragging) ----
    const QRectF roi = m_roiDraft.isNull() ? m_roi : m_roiDraft;
    if (!roi.isEmpty()) {
        const QRectF rr(roi.x() * w, roi.y() * h, roi.width() * w, roi.height() * h);
        QPen pen(m_roiDraft.isNull() ? QColor(47, 129, 247) : QColor(255, 196, 0), 1.5,
                 Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rr);
    }

    if (m_boxes.isEmpty()) {
        return;
    }

    QFont f = font();
    f.setPixelSize(qMax(10, int(h * 0.035)));

    for (const AnalysisBox &box : m_boxes) {
        // Boxes are normalized to the picture area; shift and scale them into
        // the tile, where the picture is letterboxed inside m_videoRect.
        const QRectF r(m_videoRect.x() * w + box.rect.x() * m_videoRect.width() * w,
                       m_videoRect.y() * h + box.rect.y() * m_videoRect.height() * h,
                       box.rect.width() * m_videoRect.width() * w,
                       box.rect.height() * m_videoRect.height() * h);
        if (r.width() < 2 || r.height() < 2) {
            continue;
        }
        const QColor c = colorFor(box.classId);
        p.setPen(QPen(c, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);

        if (!m_labels) {
            continue;
        }
        QString text = box.label.isEmpty() ? QString::number(box.classId) : box.label;
        if (box.trackId >= 0) {
            text = QStringLiteral("#%1 %2").arg(box.trackId).arg(text);
        }
        text += QString::fromUtf8(" %1%").arg(int(box.score * 100));

        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.boundingRect(text).width() + 8;
        const int th = fm.height() + 2;
        QRectF chip(r.left(), r.top() - th, tw, th);
        if (chip.top() < 0) {
            chip.moveTop(r.top());   // no room above: put the label inside
        }
        p.fillRect(chip, QColor(c.red(), c.green(), c.blue(), 210));
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, text);
    }
}

} // namespace zp
