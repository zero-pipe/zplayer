#ifndef ZP_ANALYSIS_OVERLAY_H
#define ZP_ANALYSIS_OVERLAY_H

#include <QVector>
#include <QWidget>

#include "analysis/analyzer.h"

namespace zp {

// Transparent painter laid over a tile's video surface. It draws detection
// boxes exactly as they came back from the analyzer — normalized to the whole
// tile frame (letterbox bars included) — so no coordinate conversion is needed
// here. It never takes mouse events, so clicking still reaches the tile.
class AnalysisOverlay : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisOverlay(QWidget *parent = nullptr);

    // Replace the drawn set. `boxes` are in normalized (0..1) tile coordinates.
    void setBoxes(const QVector<AnalysisBox> &boxes);
    void clear();

    void setLabelsVisible(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<AnalysisBox> m_boxes;
    bool m_labels = true;
};

} // namespace zp

#endif // ZP_ANALYSIS_OVERLAY_H
