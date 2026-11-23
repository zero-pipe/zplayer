#ifndef ZP_ANALYZER_H
#define ZP_ANALYZER_H

#include <QImage>
#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace zp {

// ---------------------------------------------------------------------------
// Analysis vocabulary
//
// Results are expressed in NORMALIZED VIDEO coordinates (0..1, origin at the
// top-left of the actual picture, excluding letterbox bars). Every consumer —
// overlay painter, evidence report, timeline event — converts to its own space
// with one multiply, and nothing downstream has to know the tile size, the
// DPI scale, or where the black bars are.
// ---------------------------------------------------------------------------

struct AnalysisBox {
    QRectF rect;            // normalized, origin top-left of the video area
    int classId = -1;       // analyzer-defined class id
    QString label;          // display label, already localized by the analyzer
    float score = 0.f;      // confidence 0..1
    int trackId = -1;       // stable id from the tracker, -1 = untracked
};

struct AnalysisResult {
    quint64 playerId = 0;
    quint64 frameSerial = 0;
    double pts = -1.0;      // media time of the analyzed frame
    QVector<AnalysisBox> boxes;
    QString label;          // optional free-form summary (OCR text, ...)
    // Where the analyzed picture sits in the tile frame, normalized 0..1 with
    // the origin at the top-left (letterbox bars excluded). Boxes are relative
    // to this rect, so an overlay maps a box to `videoRect.x + box.x * w`.
    QRectF videoRect = QRectF(0.0, 0.0, 1.0, 1.0);
};

// One frame handed to an analyzer: a CPU image, already downscaled by the
// service, plus the metadata needed to interpret it.
struct AnalysisFrame {
    quint64 playerId = 0;
    quint64 frameSerial = 0;
    double pts = -1.0;
    QImage image;
    // Placement of the pictured area inside the tile frame, normalized 0..1
    // (the service crops to it before analysis). Analyzers do not need it, but
    // it is carried to the result so overlays can map boxes back.
    QRectF videoRect = QRectF(0.0, 0.0, 1.0, 1.0);
};

// Implemented by concrete engines (ONNX Runtime now, TensorRT later).
// Called on the analysis worker thread, one call at a time, never on the GUI
// thread — implementations may block.
class IFrameAnalyzer {
public:
    virtual ~IFrameAnalyzer() = default;
    virtual AnalysisResult analyze(const AnalysisFrame &frame) = 0;
    virtual QString name() const = 0;
    // Class names in model index order, when the analyzer has a fixed class
    // list (YOLO). Empty = unknown / not a detector. UI reads this to show
    // model-provided labels instead of assuming COCO ids.
    virtual QStringList classNames() const { return QStringList(); }
};

} // namespace zp

Q_DECLARE_METATYPE(zp::AnalysisResult)

#endif // ZP_ANALYZER_H
