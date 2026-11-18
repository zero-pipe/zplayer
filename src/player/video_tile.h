#ifndef ZP_VIDEO_TILE_H
#define ZP_VIDEO_TILE_H

#include <QWidget>
#include <QUrl>
#include <QVector>
#include <QHash>
#include "platform/platform_api.h"
#include "player/media_player.h"
#include "analysis/analyzer.h"
#include "analysis/evidence.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
class QStackedLayout;
class QPushButton;
class QSlider;
class QHBoxLayout;
QT_END_NAMESPACE

namespace zp {

class AnalysisOverlay;

// One stream cell: a media engine renderer behind a status overlay, with a
// floating header (title, state, close/maximize). Depends only on IMediaPlayer,
// so the concrete engine (QtAV / libmpv / ...) can be swapped freely.
class VideoTile : public QWidget {
    Q_OBJECT
public:
    enum State { Idle, Connecting, Playing, Failed };

    VideoTile(const ChannelRef &ch, const QString &name, bool ptzCapable,
              QWidget *parent = nullptr);
    ~VideoTile() override;

    ChannelRef channel() const { return m_channel; }
    QString name() const { return m_name; }
    bool ptzCapable() const { return m_ptzCapable; }
    State state() const { return m_state; }

    // The engine instance behind this tile. Used by the analysis service to
    // route frames; UI code should not otherwise depend on it.
    IMediaPlayer *player() const { return m_player; }

    void play(const QUrl &url);
    void stop();
    void setSelected(bool selected);

    // Apply engine-neutral video settings (hwdec / subtitles / filters) to this
    // tile's engine. Safe before or during playback.
    void applyVideoSettings(const VideoSettings &settings);

    // Drive the overlay state without starting playback (used by the grid for
    // "connecting" placeholders and error marking before/after playStarted).
    void showConnecting(const QString &text = QString());
    void showFailed(const QString &text);

    // ---- AI overlay ----
    // Apply a detection result to this tile. Boxes are normalized to the tile
    // frame and drawn by a transparent overlay above the video.
    void setAnalysisResult(const zp::AnalysisResult &result);

    // ---- AI fast-forward ("skip empty") ----
    // When enabled and the engine supports playback control, spans with no
    // person on screen are skipped forward while playing. This is the
    // edge-playback mode; the offline pre-scan will feed the same decision.
    void setAiSkipEnabled(bool on);
    bool aiSkipEnabled() const { return m_aiSkip; }

    // ---- Evidence ----
    // Write the events collected for this tile into outDir (report + thumbs).
    bool exportEvidence(const QString &outDir, QString *error = nullptr);
    int evidenceEventCount() const { return m_evidence.size(); }

signals:
    void clickedTile(const zp::ChannelRef &ch);
    void closeRequested(const zp::ChannelRef &ch);
    void retryRequested(const zp::ChannelRef &ch);
    void maximizeRequested(const zp::ChannelRef &ch);
    // Emitted after the footer's snapshot action. path is where it was saved.
    void snapshotTaken(const zp::ChannelRef &ch, const QString &path, bool ok);
    // User pressed the evidence button; the window picks a folder, then calls
    // exportEvidence() and reports the result.
    void evidenceExportRequested(const zp::ChannelRef &ch);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(State state, const QString &text);
    void refreshSelectionStyle();
    void takeSnapshot();

    // Transport bar (only built when the engine supports playback control).
    void buildTransport(QHBoxLayout *row);
    void updateTransportUi();
    void cycleSpeed();
    static QString formatTime(double seconds);

    // AI helpers.
    void updateAiSkip(double pts, const QVector<AnalysisBox> &boxes);
    void updateEvidence(double pts, const QVector<AnalysisBox> &boxes);
    void closeOpenEvidence(double atPts, bool all);
    QString captureEvidenceThumbnail();

    ChannelRef m_channel;
    QString m_name;
    bool m_ptzCapable = false;
    bool m_selected = false;
    State m_state = Idle;

    IMediaPlayer *m_player = nullptr;   // engine, owns its renderer
    QWidget *m_video = nullptr;         // renderer surface inserted in the stage
    QStackedLayout *m_stage = nullptr;
    QWidget *m_overlay = nullptr;
    QLabel *m_overlayText = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_title = nullptr;
    QWidget *m_footer = nullptr;        // bottom action bar (snapshot, ...)

    AnalysisOverlay *m_aiOverlay = nullptr;   // detection boxes over the video

    // Transport.
    bool m_hasTransport = false;
    QToolButton *m_playBtn = nullptr;
    QSlider *m_posSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QToolButton *m_speedBtn = nullptr;
    QToolButton *m_aiBtn = nullptr;
    QToolButton *m_evidenceBtn = nullptr;
    double m_duration = -1.0;
    int m_speedIndex = 0;
    bool m_scrubbing = false;

    // AI fast-forward state.
    bool m_aiSkip = false;
    qint64 m_lastPresenceMs = 0;
    qint64 m_lastSeekMs = 0;

    // Evidence accumulation. Open intervals are keyed by class id.
    QVector<EvidenceEvent> m_evidence;
    QHash<int, int> m_openEvents;   // classId -> index into m_evidence
    double m_lastPts = -1.0;
};

} // namespace zp

#endif // ZP_VIDEO_TILE_H
