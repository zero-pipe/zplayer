#include "train/annotation_canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace zp {

namespace {
QColor classColor(int id)
{
    // A small, stable palette so boxes are distinguishable at a glance.
    static const QColor palette[] = {
        QColor(0xEA, 0x43, 0x35), QColor(0x1A, 0x73, 0xE8),
        QColor(0x34, 0xA8, 0x53), QColor(0xFB, 0xBC, 0x05),
        QColor(0x9C, 0x27, 0xB0), QColor(0x00, 0x88, 0x8A),
    };
    if (id < 0) {
        return QColor(0xF9, 0xAB, 0x00);
    }
    return palette[id % int(sizeof(palette) / sizeof(palette[0]))];
}
} // namespace

AnnotationCanvas::AnnotationCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(360, 210);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setAutoFillBackground(true);
}

void AnnotationCanvas::setImage(const QImage &image)
{
    m_image = image;
    m_sel = -1;
    update();
    emit selectionChanged(-1);
}

void AnnotationCanvas::setBoxes(const QVector<AnalysisBox> &boxes)
{
    m_boxes = boxes;
    m_sel = -1;
    update();
    emit boxesChanged();
    emit selectionChanged(-1);
}

void AnnotationCanvas::selectBox(int index)
{
    if (index < -1 || index >= m_boxes.size() || index == m_sel) {
        return;
    }
    m_sel = index;
    update();
    emit selectionChanged(m_sel);
}

void AnnotationCanvas::addBoxCentered()
{
    AnalysisBox b;
    b.classId = m_currentClass;
    b.label = m_currentClassLabel.isEmpty() ? QStringLiteral("#%1").arg(m_currentClass)
                                            : m_currentClassLabel;
    b.rect = QRectF(0.40, 0.38, 0.20, 0.24);
    b.score = 0.f;
    m_boxes.append(b);
    m_sel = m_boxes.size() - 1;
    update();
    emit boxesChanged();
    emit selectionChanged(m_sel);
}

void AnnotationCanvas::setCurrentClass(int classId, const QString &label)
{
    m_currentClass = classId;
    m_currentClassLabel = label;
}

void AnnotationCanvas::setSelectedClass(int classId, const QString &label)
{
    if (m_sel >= 0 && m_sel < m_boxes.size()) {
        m_boxes[m_sel].classId = classId;
        if (!label.isEmpty()) {
            m_boxes[m_sel].label = label;
        }
        update();
        emit boxesChanged();
    }
}

QRectF AnnotationCanvas::targetRect() const
{
    const QRectF r = rect().adjusted(8, 8, -8, -8);
    if (m_image.isNull() || r.width() <= 0 || r.height() <= 0) {
        return r;
    }
    const double ar = double(m_image.width()) / double(m_image.height());
    double w = r.width();
    double h = w / ar;
    if (h > r.height()) {
        h = r.height();
        w = h * ar;
    }
    const double x = r.x() + (r.width() - w) / 2.0;
    const double y = r.y() + (r.height() - h) / 2.0;
    return QRectF(x, y, w, h);
}

QPointF AnnotationCanvas::toNorm(const QPointF &p) const
{
    const QRectF t = targetRect();
    if (t.width() <= 0 || t.height() <= 0) {
        return QPointF(0, 0);
    }
    return QPointF((p.x() - t.x()) / t.width(), (p.y() - t.y()) / t.height());
}

int AnnotationCanvas::hitBox(const QPointF &p, bool *onHandle) const
{
    const QRectF t = targetRect();
    if (onHandle) {
        *onHandle = false;
    }
    for (int i = m_boxes.size() - 1; i >= 0; --i) {
        const QRectF br(m_boxes[i].rect.x() * t.width() + t.x(),
                        m_boxes[i].rect.y() * t.height() + t.y(),
                        m_boxes[i].rect.width() * t.width(),
                        m_boxes[i].rect.height() * t.height());
        if (i == m_sel) {
            const QPointF corners[4] = { br.topLeft(), br.topRight(),
                                         br.bottomLeft(), br.bottomRight() };
            for (const QPointF &c : corners) {
                if (QRectF(c.x() - 6, c.y() - 6, 12, 12).contains(p)) {
                    if (onHandle) {
                        *onHandle = true;
                    }
                    return i;
                }
            }
        }
        if (br.contains(p)) {
            return i;
        }
    }
    return -1;
}

void AnnotationCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0B, 0x12, 0x20));
    const QRectF t = targetRect();
    if (!m_image.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(t, m_image);
    }

    const QColor accent(0x25, 0x63, 0xEB);
    const QColor rose(0xE1, 0x1D, 0x48);

    QFont f = font();
    f.setPixelSize(qMax(11, int(t.height() * 0.028)));
    p.setFont(f);

    for (int i = 0; i < m_boxes.size(); ++i) {
        const AnalysisBox &b = m_boxes[i];
        const QRectF br(b.rect.x() * t.width() + t.x(),
                        b.rect.y() * t.height() + t.y(),
                        b.rect.width() * t.width(),
                        b.rect.height() * t.height());
        // Selected = blue; low-confidence = rose; otherwise class colour.
        QColor c = (b.score > 0.f && b.score < 0.5f) ? rose : classColor(b.classId);
        if (i == m_sel) {
            c = accent;
        }
        QPen pen(c, i == m_sel ? 2.4 : 2.0);
        p.setPen(pen);
        p.setBrush(i == m_sel ? QColor(c.red(), c.green(), c.blue(), 32) : Qt::NoBrush);
        p.drawRect(br);
        p.setBrush(Qt::NoBrush);

        QString label = b.label.isEmpty() ? QStringLiteral("#%1").arg(b.classId) : b.label;
        if (b.score > 0.f) {
            label += QStringLiteral(" %1%").arg(int(b.score * 100));
        }
        const QFontMetrics fm(f);
        const int tw = fm.boundingRect(label).width() + 10;
        const int th = fm.height() + 2;
        QRectF chip(br.left(), br.top() - th, tw, th);
        if (chip.top() < t.top()) {
            chip.moveTop(br.top());
        }
        QPainterPath chipPath;
        chipPath.addRoundedRect(chip, 2, 2);
        p.fillPath(chipPath, c);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, label);

        if (i == m_sel) {
            const QPointF corners[4] = { br.topLeft(), br.topRight(),
                                         br.bottomLeft(), br.bottomRight() };
            for (const QPointF &cf : corners) {
                const QRectF h(cf.x() - 3.5, cf.y() - 3.5, 7, 7);
                p.setPen(QPen(accent, 1.5));
                p.setBrush(Qt::white);
                p.drawRoundedRect(h, 1.5, 1.5);
            }
        }
    }
}

void AnnotationCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }
    bool onHandle = false;
    const int hit = hitBox(e->pos(), &onHandle);
    const int prev = m_sel;
    m_sel = hit;
    if (hit >= 0) {
        m_mode = onHandle ? Resize : Move;
        m_pressNorm = toNorm(e->pos());
        m_origRect = m_boxes[hit].rect;
    } else {
        m_mode = None;
    }
    update();
    if (prev != m_sel) {
        emit selectionChanged(m_sel);
    }
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_mode == None || m_sel < 0 || m_sel >= m_boxes.size()) {
        return;
    }
    const QPointF n = toNorm(e->pos());
    const double dx = n.x() - m_pressNorm.x();
    const double dy = n.y() - m_pressNorm.y();
    QRectF &r = m_boxes[m_sel].rect;
    if (m_mode == Move) {
        r.moveTo(qBound(0.0, m_origRect.x() + dx, 1.0 - m_origRect.width()),
                 qBound(0.0, m_origRect.y() + dy, 1.0 - m_origRect.height()));
    } else {
        r.setWidth(qBound(0.03, m_origRect.width() + dx, 1.0 - m_origRect.x()));
        r.setHeight(qBound(0.03, m_origRect.height() + dy, 1.0 - m_origRect.y()));
    }
    update();
    emit boxesChanged();
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *)
{
    m_mode = None;
}

void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Double-click on empty area adds a default box centred at the click.
    bool onHandle = false;
    if (hitBox(e->pos(), &onHandle) >= 0) {
        return;
    }
    const QPointF n = toNorm(e->pos());
    AnalysisBox b;
    b.classId = m_currentClass;
    b.rect = QRectF(qBound(0.0, n.x() - 0.10, 0.80), qBound(0.0, n.y() - 0.12, 0.80), 0.20, 0.24);
    b.label = m_currentClassLabel.isEmpty() ? QStringLiteral("#%1").arg(m_currentClass)
                                            : m_currentClassLabel;
    b.score = 0.f;
    m_boxes.append(b);
    m_sel = m_boxes.size() - 1;
    update();
    emit boxesChanged();
    emit selectionChanged(m_sel);
}

void AnnotationCanvas::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && m_sel >= 0 && m_sel < m_boxes.size()) {
        m_boxes.remove(m_sel);
        m_sel = -1;
        update();
        emit boxesChanged();
        emit selectionChanged(-1);
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace zp
