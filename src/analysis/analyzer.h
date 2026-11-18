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
    float score = 0.f;      // confidence 0..1
    int trackId = -1;       // stable id from the tracker, -1 = untracked
};

struct AnalysisResult {
    quint64 playerId = 0;
    quint64 frameSerial = 0;
    double pts = -1.0;      // media time of the analyzed frame
    QVector<AnalysisBox> boxes;
    QString label;          // optional free-form summary (OCR text, ...)
};

// One frame handed to an analyzer: a CPU image, already downscaled by the
// service, plus the metadata needed to interpret it.
struct AnalysisFrame {
    quint64 playerId = 0;
    quint64 frameSerial = 0;
    double pts = -1.0;
    QImage image;
};

// Implemented by concrete engines (ONNX Runtime now, TensorRT later).
// Called on the analysis worker thread, one call at a time, never on the GUI
// thread — implementations may block.
class IFrameAnalyzer {
public:
    virtual ~IFrameAnalyzer() = default;
    virtual AnalysisResult analyze(const AnalysisFrame &frame) = 0;
    virtual QString name() const = 0;
};

} // namespace zp

Q_DECLARE_METATYPE(zp::AnalysisResult)

#endif // ZP_ANALYZER_H
