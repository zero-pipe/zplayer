#ifndef ZP_OCR_DETECTOR_H
#define ZP_OCR_DETECTOR_H

#include <QString>
#include <QStringList>
#include <memory>

#include "analysis/analyzer.h"

namespace zp {

// Class id reported for text boxes, above any COCO id, so overlay colors and
// downstream logic can tell OCR boxes from object detections.
constexpr int kTextClassId = 1000;

// ---------------------------------------------------------------------------
// Scene-text OCR on ONNX Runtime: PP-OCRv4 mobile det + rec (Chinese dict).
//
// Runs through the same IFrameAnalyzer contract as the object detector, so a
// tile can switch its analysis from "what objects" to "what does it say"
// without any new plumbing. Typically used together with an ROI: the user
// frames a sign / plate / screen, and only that region is OCR'd.
//
// Pipeline: DB text detection over the downscaled analysis frame, then CTC
// recognition of each detected line, boxes reported in the analyzer's own
// normalized coordinates with the recognized string as the label.
// ---------------------------------------------------------------------------
class OcrDetector : public IFrameAnalyzer {
public:
    OcrDetector();
    ~OcrDetector() override;

    // Load det/rec models plus the character dictionary. Returns false and
    // fills `error` on failure.
    bool load(const QString &detModel, const QString &recModel, const QString &dictPath,
              QString *error = nullptr);

    bool isLoaded() const;
    QString backendName() const;

    AnalysisResult analyze(const AnalysisFrame &frame) override;
    QString name() const override;

    void setDetThreshold(float t);      // default 0.30
    void setMinTextScore(float s);      // default 0.50 (mean CTC confidence)

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// Locate the OCR models. Order:
//   1. ZP_OCR_DIR (directory containing the three files below)
//   2. <appdir>/models/ocr, <appdir>/../3rdpart/models/ocr, ...
// File names: ch_PP-OCRv4_det_mobile.onnx, ch_PP-OCRv4_rec_mobile.onnx,
//             ppocr_keys_v1.txt
bool findDefaultOcrModels(QString *detModel, QString *recModel, QString *dictPath,
                          QString *error);

} // namespace zp

#endif // ZP_OCR_DETECTOR_H
