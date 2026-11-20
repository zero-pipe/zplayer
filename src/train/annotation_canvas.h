#ifndef ZP_ANNOTATION_CANVAS_H
#define ZP_ANNOTATION_CANVAS_H

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include "analysis/analyzer.h"

namespace zp {

// Minimal but real bounding-box editor over a still frame: move / resize /
// add (double-click empty) / delete (Del). Boxes are stored normalized 0..1
// relative to the picture, matching AnalysisBox convention.
class AnnotationCanvas : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationCanvas(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    QImage image() const { return m_image; }

    void setBoxes(const QVector<AnalysisBox> &boxes);
    QVector<AnalysisBox> boxes() const { return m_boxes; }

    void setCurrentClass(int classId);
    int selectedIndex() const { return m_sel; }
    void setSelectedClass(int classId);
    // Programmatic selection (inspector list -> canvas). -1 clears.
    void selectBox(int index);
    // Append a default box near the centre using the current class.
    void addBoxCentered();

signals:
    void boxesChanged();
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    QRectF targetRect() const;                 // where the image is drawn
    QPointF toNorm(const QPointF &widgetPt) const;
    int hitBox(const QPointF &widgetPt, bool *onHandle) const;

    QImage m_image;
    QVector<AnalysisBox> m_boxes;
    int m_sel = -1;
    int m_currentClass = 0;

    enum Mode { None, Move, Resize } m_mode = None;
    QPointF m_pressNorm;
    QRectF m_origRect;
};

} // namespace zp

#endif // ZP_ANNOTATION_CANVAS_H
