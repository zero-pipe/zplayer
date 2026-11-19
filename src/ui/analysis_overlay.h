#ifndef ZP_ANALYSIS_OVERLAY_H
#define ZP_ANALYSIS_OVERLAY_H

#include <QVector>
#include <QWidget>

#include "analysis/analyzer.h"

namespace zp {

// Transparent painter laid over a tile's video surface. Boxes arrive
// normalized to the picture area (letterbox bars excluded), and `videoRect`
// says where that area sits in the tile frame — this is the only place the two
// spaces are combined, so nothing upstream has to know the tile size or DPI.
// The overlay never takes mouse events, so clicking still reaches the tile.
class AnalysisOverlay : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisOverlay(QWidget *parent = nullptr);

    // Replace the drawn set. `boxes` are normalized (0..1) to `videoRect`,
    // which is itself normalized to the whole tile frame.
    void setBoxes(const QVector<AnalysisBox> &boxes,
                  const QRectF &videoRect = QRectF(0.0, 0.0, 1.0, 1.0));
    void clear();

    void setLabelsVisible(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<AnalysisBox> m_boxes;
    QRectF m_videoRect = QRectF(0.0, 0.0, 1.0, 1.0);
    bool m_labels = true;
};

} // namespace zp

#endif // ZP_ANALYSIS_OVERLAY_H
