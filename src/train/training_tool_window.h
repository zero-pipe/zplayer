#ifndef ZP_TRAINING_TOOL_WINDOW_H
#define ZP_TRAINING_TOOL_WINDOW_H

#include <QDialog>
#include <QVector>

#include "train/dataset_store.h"

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

namespace zp {

class AnnotationCanvas;

// Independent, local labelling + training workbench (separate window from the
// player). Six stages along the left nav: queue, annotate, QC, train, evaluate,
// publish. Visual language follows docs/zeroo_ai_prototype.
class TrainingToolWindow : public QDialog {
    Q_OBJECT
public:
    explicit TrainingToolWindow(QWidget *parent = nullptr);

    // Jump to the annotation page for a specific sample (by its base id).
    // Falls back to the queue when the id is unknown.
    void openRecordById(const QString &id);
    // Dev helper: switch to a stage (0..5) before the window is shown.
    void goToStage(int index);

signals:
    void sampleCountChanged(int count);
    void modelVersionChanged(const QString &version);

private:
    void buildUi();
    QWidget *buildNav();
    QWidget *buildQueuePage();
    QWidget *buildAnnotatePage();
    QWidget *buildQcPage();
    QWidget *buildTrainPage();
    QWidget *buildEvalPage();
    QWidget *buildPublishPage();

    void reload();
    void reloadQueue();
    void openRecord(int index);
    void saveCurrent();
    void showPage(int index);
    void refreshQc();
    void rebuildClassButtons();
    void openClassManager();
    void exportDataset();
    void startTraining();
    void onTrainFinished(int exitCode);
    void refreshEval();
    void refreshPublish();
    void setCurrentVersion(const QString &name);

    // Annotation inspector helpers.
    void refreshInspector();
    void syncInspectorSelection(int index);
    int selectedQueueRowsCount() const;
    void updateAnnotateButtons();
    void setConsoleState(bool running);

    QWidget *m_nav = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QVector<QPushButton *> m_navBtns;
    QLabel *m_navQueueBadge = nullptr;
    QLabel *m_navQcBadge = nullptr;
    QLabel *m_navFootVal = nullptr;
    QStackedWidget *m_stack = nullptr;
    int m_page = 0;

    // Queue page
    QTableWidget *m_queue = nullptr;
    QLabel *m_queueInfo = nullptr;
    QLineEdit *m_queueSearch = nullptr;
    QPushButton *m_annotateBtn = nullptr;
    bool m_loadingQueue = false;

    // Annotation page
    AnnotationCanvas *m_canvas = nullptr;
    QLabel *m_annoName = nullptr;
    QLabel *m_annoReason = nullptr;
    QLabel *m_annoIndex = nullptr;
    QLabel *m_boxCount = nullptr;
    QListWidget *m_boxList = nullptr;
    QButtonGroup *m_classGroup = nullptr;
    QGridLayout *m_classGrid = nullptr;   // rebuilt when the class table changes
    bool m_syncingInspector = false;

    // QC page
    QLabel *m_kpiTotal = nullptr;
    QLabel *m_kpiDone = nullptr;
    QLabel *m_kpiTodo = nullptr;
    QLabel *m_kpiBoxes = nullptr;
    QLabel *m_qcBadge = nullptr;
    QVector<QProgressBar *> m_distBars;
    QVector<QLabel *> m_distVals;
    QVBoxLayout *m_distHost = nullptr;   // holds one row per project class
    QLabel *m_qcPath = nullptr;
    QString m_lastExportDir;

    // Training page
    QSpinBox *m_epochs = nullptr;
    QSpinBox *m_batch = nullptr;
    QComboBox *m_imgsz = nullptr;
    QComboBox *m_device = nullptr;
    QLineEdit *m_trainCmd = nullptr;
    QPushButton *m_startBtn = nullptr;
    QLabel *m_trainStatus = nullptr;
    QLabel *m_trainPct = nullptr;
    QProgressBar *m_trainProg = nullptr;
    QPlainTextEdit *m_trainLog = nullptr;
    QLabel *m_consoleBadge = nullptr;
    QProcess *m_proc = nullptr;

    // Eval page
    QLabel *m_evalInfo = nullptr;
    QLabel *m_evMap50 = nullptr;
    QLabel *m_evMap5095 = nullptr;
    QLabel *m_evPrec = nullptr;
    QLabel *m_evRecall = nullptr;
    QLabel *m_evDelta = nullptr;
    QLabel *m_cmpCurLabel = nullptr;
    QLabel *m_cmpPrevLabel = nullptr;
    QProgressBar *m_cmpCurBar = nullptr;
    QProgressBar *m_cmpPrevBar = nullptr;

    // Publish page
    QListWidget *m_verList = nullptr;
    QLabel *m_pubInfo = nullptr;

    QVector<SampleRecord> m_records;
    int m_current = -1;
};

} // namespace zp

#endif // ZP_TRAINING_TOOL_WINDOW_H
