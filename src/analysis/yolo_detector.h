#ifndef ZP_YOLO_DETECTOR_H
#define ZP_YOLO_DETECTOR_H

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

#include "analysis/analyzer.h"

namespace zp {

// ---------------------------------------------------------------------------
// YOLOv8 / YOLO11 object detector running on ONNX Runtime.
//
// Used through the engine-neutral IFrameAnalyzer interface: AnalysisService
// hands it throttled, downscaled CPU frames; it returns boxes normalized to
// the analyzed image (0..1), which is exactly what the overlay needs.
//
// Backend: DirectML when a D3D12 device is available, CPU otherwise. The
// bundled ORT is the DirectML build; on this toolchain only the C API is used
// (see 3rdpart/onnxruntime/onnxruntime.pri).
// ---------------------------------------------------------------------------
class YoloDetector : public IFrameAnalyzer {
public:
    YoloDetector();
    ~YoloDetector() override;

    // Load a YOLO ONNX model plus its class-name list. On failure returns
    // false and fills `error`.
    bool load(const QString &modelPath, const QString &classesPath,
              QString *error = nullptr);

    bool isLoaded() const;
    QString backendName() const;   // "DirectML" / "CPU" / ""
    QString modelPath() const;

    AnalysisResult analyze(const AnalysisFrame &frame) override;
    QString name() const override;

    void setConfidenceThreshold(float t);
    void setNmsThreshold(float t);
    // Only report these class ids when non-empty (empty = all classes).
    void setClassFilter(const QList<int> &classIds);

    static QColor classColor(int classId);
    static QString classNameFor(int classId);   // global COCO names
    static QString localizedClassName(int classId);  // Chinese label, English fallback

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// Locate the default YOLO model + class list. Order:
//   1. ZP_MODEL / ZP_CLASSES environment overrides
//   2. <appdir>/models/, <appdir>/../3rdpart/models/, <appdir>/3rdpart/models/
// Returns false and fills `error` when nothing usable is found. `classesPath`
// may come back empty when the model has an embedded/metadata name list.
bool findDefaultYoloModel(QString *modelPath, QString *classesPath, QString *error);

// COCO class ids that count as "person". Kept in one place so presence / skip /
// evidence logic never hard-codes a magic number.
constexpr int kPersonClassId = 0;

} // namespace zp

#endif // ZP_YOLO_DETECTOR_H
