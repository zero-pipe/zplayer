#ifndef ZP_VIDEO_TILE_H
#define ZP_VIDEO_TILE_H

#include <QWidget>
#include <QUrl>
#include <QSet>
#include <QVector>
#include <QHash>
#include "platform/platform_api.h"
#include "player/media_player.h"
#include "analysis/analyzer.h"
#include "analysis/evidence.h"
#include "analysis/timeline.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
class QStackedLayout;
class QPushButton;
class QSlider;
class QMenu;
class TimelineSlider;
class QHBoxLayout;
QT_END_NAMESPACE

namespace zp {

class AnalysisOverlay;
class TimelineSlider;

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
    const QVector<EvidenceEvent> &evidenceEvents() const { return m_evidence; }
    QString mediaName() const { return m_name.isEmpty() ? m_channel.channelId : m_name; }

    // ---- Dev/test hooks (see ZP_OCR / ZP_ROI in PlayerWindow::fillDemo) ----
    void setOcrEnabled(bool on);            // same as clicking 文字
    void setRoiRect(const QRectF &roi);     // normalized to the tile frame
    QRectF roiRect() const { return m_roi; }
    void setZoomEnabled(bool on);           // same as clicking 放大
    void setSharpenEnabled(bool on);        // same as clicking 超分

    // ---- Smart timeline ----
    // Fast offline pass over the file at high playback speed, recording when
    // people are on screen; the result is painted on the transport slider.
    // Public so the demo hooks can drive it headlessly.
    void startScan();
    bool isScanning() const { return m_scanning; }

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
    // ROI changed (normalized to the tile frame; empty = cleared).
    void roiChanged(const zp::ChannelRef &ch, const QRectF &roi);
    // Per-tile OCR analysis toggled.
    void ocrToggled(const zp::ChannelRef &ch, bool on);
    // Offline scan finished; `segments` = number of person intervals found.
    void scanFinished(const zp::ChannelRef &ch, int segments);
    // Transient status text for the window-level toast.
    void statusMessage(const zp::ChannelRef &ch, const QString &text);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(State state, const QString &text);
    void refreshSelectionStyle();
    void takeSnapshot();

    // Transport bar (only built when the engine supports playback control).
    void updateTransportUi();
    void setSpeed(double speed);
    static QString formatTime(double seconds);

    // Floating glass control bar over the video (built on the video widget).
    void buildControlBar();
    void positionControlBar();
    void updateEvidenceBadge();

    // AI helpers.
    void updateAiSkip(double pts, const QVector<AnalysisBox> &boxes);
    void updateEvidence(double pts, const QVector<AnalysisBox> &boxes);
    void closeOpenEvidence(double atPts, bool all);
    QString captureEvidenceThumbnail();

    // Smart timeline: scan driving.
    void finishScan();
    void cancelScan();

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
    TimelineSlider *m_posSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QToolButton *m_speedBtn = nullptr;
    QMenu *m_speedMenu = nullptr;
    QToolButton *m_snapBtn = nullptr;
    QToolButton *m_aiBtn = nullptr;
    QToolButton *m_aiMenuBtn = nullptr;
    QToolButton *m_evidenceBtn = nullptr;
    QToolButton *m_roiBtn = nullptr;
    QToolButton *m_ocrBtn = nullptr;
    QToolButton *m_zoomBtn = nullptr;
    QToolButton *m_sharpenBtn = nullptr;
    double m_duration = -1.0;
    bool m_scrubbing = false;
    bool m_hasPlayed = false;   // a stream has been shown (keep last frame)
    bool m_hovered = false;     // pointer over the video (small-tile bar)

    // ROI selection (normalized to the video widget).
    bool m_roiMode = false;       // waiting for / receiving a drag
    bool m_draggingRoi = false;
    QPointF m_roiStart;
    QRectF m_roi;

    // Electronic zoom: 0 = off (1x), 1 = 2x, 2 = 4x. The view crops a
    // centered window inside the ROI (or the whole video) and can be panned.
    int m_zoomLevel = 0;
    QPointF m_zoomCenter = QPointF(0.5, 0.5);   // tile-normalized
    bool m_panning = false;
    QPointF m_panLast;
    void applyZoom();
    QRectF zoomCropRect() const;
    void setZoomLevel(int level);

    // Smart timeline scan state.
    QToolButton *m_scanBtn = nullptr;
    bool m_scanning = false;
    bool m_scanPrevMuted = false;
    double m_scanPrevSpeed = 1.0;
    double m_scanPrevPos = 0.0;
    QVector<PresenceSample> m_scanSamples;

    // AI fast-forward state.
    bool m_aiSkip = false;
    bool m_aiPauseOnHit = false;      // pause instead of resuming 1x on a hit
    bool m_aiSkipping = false;        // a skip just happened, awaiting a hit
    QSet<int> m_aiClasses;            // class ids that count as "present"
    qint64 m_lastPresenceMs = 0;
    qint64 m_lastSeekMs = 0;

    // Evidence accumulation. Open intervals are keyed by class id.
    QVector<EvidenceEvent> m_evidence;
    QHash<int, int> m_openEvents;   // classId -> index into m_evidence
    double m_lastPts = -1.0;
};

} // namespace zp

#endif // ZP_VIDEO_TILE_H
