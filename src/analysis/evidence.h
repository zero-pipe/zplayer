#ifndef ZP_EVIDENCE_H
#define ZP_EVIDENCE_H

#include <QString>
#include <QVector>

namespace zp {

// One detected target that was on screen for a contiguous span of the media.
// Times are media seconds; a span is merged from analysis frames so a person
// walking through the scene yields a single event, not one per analyzed frame.
struct EvidenceEvent {
    double start = 0.0;
    double end = 0.0;
    QString label;            // localized class name ("人", "车", ...)
    int classId = -1;
    int peakCount = 1;        // max simultaneous boxes of this class
    QString thumbSource;      // absolute path of a snapshot to include (optional)
};

// Writes a self-contained evidence folder: report.html (human readable),
// report.json (machine readable), and copies of the event thumbnails. The
// report intentionally carries normalized facts only (times, labels, counts),
// so it is stable regardless of tile size / DPI / window layout.
class EvidenceExporter {
public:
    static bool writeReport(const QString &mediaName,
                            const QVector<EvidenceEvent> &events,
                            const QString &outDir,
                            QString *error = nullptr);
};

} // namespace zp

#endif // ZP_EVIDENCE_H
