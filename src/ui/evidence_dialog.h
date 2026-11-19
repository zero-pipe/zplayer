#ifndef ZP_EVIDENCE_DIALOG_H
#define ZP_EVIDENCE_DIALOG_H

#include <QDialog>
#include <QVector>

#include "analysis/evidence.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
QT_END_NAMESPACE

namespace zp {

// Evidence review before export: every collected event with its snapshot,
// time span and count; the user picks what to include, then exports a
// self-contained folder (report.html + report.json + thumbnails).
class EvidenceDialog : public QDialog {
    Q_OBJECT
public:
    EvidenceDialog(const QString &mediaName, const QVector<EvidenceEvent> &events,
                   QWidget *parent = nullptr);

    // Events currently ticked.
    QVector<EvidenceEvent> selectedEvents() const;

signals:
    void exportRequested(const QVector<EvidenceEvent> &events);

private:
    void buildUi(const QString &mediaName);
    void setAllChecked(bool checked);
    void updateSummary();

    QVector<EvidenceEvent> m_events;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace zp

#endif // ZP_EVIDENCE_DIALOG_H
