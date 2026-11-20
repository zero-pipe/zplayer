#ifndef ZP_DATASET_STORE_H
#define ZP_DATASET_STORE_H

#include <QString>
#include <QVector>

#include "analysis/analyzer.h"

namespace zp {

// One captured hard sample (a frame + the boxes the model predicted on it).
struct SampleRecord {
    QString id;          // frame base name (no path)
    QString framePath;   // absolute path to the saved image
    QString labelPath;   // absolute path to the YOLO label file
    QString channel;     // human name of the source channel
    QString reason;      // "low_conf" | "manual"
    double pts = -1.0;   // media time
    bool done = false;   // already reviewed / labelled
    QVector<AnalysisBox> boxes;
};

// Local, single-machine dataset of failed / suspicious frames. Stores images +
// a JSONL manifest under AppData/.../dataset. No server involved.
class DatasetStore {
public:
    static DatasetStore &instance();

    void setRoot(const QString &root);
    QString root() const { return m_root; }

    QString framesDir() const;
    QString labelsDir() const;
    void ensureDirs() const;

    // Unique path for a new frame belonging to `channel`.
    QString newFramePath(const QString &channel) const;

    // Record an already-written frame image + its predicted boxes.
    SampleRecord add(const QString &framePath, const QString &channel, double pts,
                     const QString &reason, const QVector<AnalysisBox> &boxes);

    QVector<SampleRecord> load() const;
    void rewrite(const QVector<SampleRecord> &records) const;

    // Write the (edited) boxes as a YOLO txt next to the frame.
    bool saveLabels(const SampleRecord &rec, const QVector<AnalysisBox> &boxes) const;

    // Export a train/val YOLO dataset (images + labels + data.yaml) to outDir.
    // Split is grouped by channel so frames from one camera do not leak across
    // the train/val boundary.
    bool exportYolo(const QString &outDir, double valRatio, QString *error) const;

    int count() const;          // cached count of manifest entries
    void refreshCount();
    void clear();

private:
    DatasetStore();
    QString manifestPath() const;
    QString sanitize(const QString &s) const;

    QString m_root;
    mutable int m_count = 0;
};

} // namespace zp

#endif // ZP_DATASET_STORE_H
