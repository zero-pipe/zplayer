#ifndef ZP_VIDEO_GRID_H
#define ZP_VIDEO_GRID_H

#include <QWidget>
#include <QVector>
#include <QUrl>
#include "platform/platform_api.h"
#include "player/media_player.h"

QT_BEGIN_NAMESPACE
class QGridLayout;
class QStackedLayout;
QT_END_NAMESPACE

namespace zp {

class VideoTile;
class AnalysisService;
class IFrameAnalyzer;

// Split-screen container. Manages a slot-based grid so irregular security
// layouts (e.g. 1 big + 5 small) are expressible.
class VideoGrid : public QWidget {
    Q_OBJECT
public:
    enum Layout { L1x1, L2x2, L3x3, L4x4, L1p5 };

    explicit VideoGrid(QWidget *parent = nullptr);
    ~VideoGrid() override;

    void setLayoutKind(Layout kind);
    Layout layoutKind() const { return m_kind; }

    // Open (or re-focus) a stream in the grid.
    void openStream(const zp::ChannelRef &ch, const QString &name,
                    bool ptzCapable, const QUrl &url);
    void closeStream(const zp::ChannelRef &ch);
    void stopAll();

    bool contains(const zp::ChannelRef &ch) const;
    int tileCount() const;

    // Maximize a channel into a 1x1 layout (single view).
    void setSingleChannel(const zp::ChannelRef &ch);

    // Focus an existing channel's tile (no playback changes).
    void focusChannel(const zp::ChannelRef &ch);
    // Show a connecting placeholder for a channel before its stream URL arrives.
    void showLoadingPlaceholder(const zp::ChannelRef &ch, const QString &name);
    // Mark a channel's tile failed / back to connecting.
    void markFailed(const zp::ChannelRef &ch, const QString &message);
    void markConnecting(const zp::ChannelRef &ch);

    // Grid-wide default video settings (hwdec / subtitles / filters). Stored so
    // newly created tiles inherit them, and pushed to all live tiles now.
    void setVideoSettings(const VideoSettings &settings);
    VideoSettings videoSettings() const { return m_settings; }

    // Route every tile's frames to the given service (null = no analysis).
    // Tiles created later are attached automatically, and each result is
    // delivered to the owning tile for overlay/evidence.
    void setAnalysisService(AnalysisService *service);

    // Analyzer used by tiles that turn on OCR (not owned; may be null when the
    // OCR models are absent, in which case the toggle simply does nothing).
    void setOcrAnalyzer(IFrameAnalyzer *analyzer) { m_ocrAnalyzer = analyzer; }

    // Class names of the active detector (index order). Tiles use them to show
    // model-provided labels and to decide whether COCO semantics (人/车/狗) apply.
    void setClassNames(const QStringList &names);
    QStringList classNames() const { return m_classNames; }

    // Global "collect failure samples" switch (persisted by the window). Applies
    // to every tile, now and in the future.
    void setCaptureEnabled(bool on);
    bool captureEnabled() const { return m_captureEnabled; }

    // Tile lookup for window-level actions (AI toggle, evidence export).
    VideoTile *tileFor(const zp::ChannelRef &ch) const;
    VideoTile *activeTile() const;
    const QVector<VideoTile *> &tiles() const { return m_tiles; }

signals:
    void activeChannelChanged(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void streamClosed(const zp::ChannelRef &ch);
    void retryRequested(const zp::ChannelRef &ch);
    void maximizeRequested(const zp::ChannelRef &ch);
    void layoutChanged(int index);
    void snapshotTaken(const zp::ChannelRef &ch, const QString &path, bool ok);
    void evidenceExportRequested(const zp::ChannelRef &ch);
    // Offline smart-timeline scan finished for a tile.
    void streamScanFinished(const zp::ChannelRef &ch, int segments);
    // Transient tile status text (control-bar toggles), shown as a toast.
    void tileStatus(const zp::ChannelRef &ch, const QString &text);
    // A tile started/stopped playing (used to gate the PTZ panel).
    void tilePlaybackStateChanged(const zp::ChannelRef &ch, bool playing);
    // A tile flipped the global "collect samples" switch; the window persists
    // it and calls setCaptureEnabled() so every tile follows.
    void captureSettingChanged(bool on);
    // A frame was collected into the dataset (manual or automatic).
    void sampleCaptured(const zp::ChannelRef &ch, bool ok);

private:
    struct Slot { int row, col, rowSpan, colSpan; };
    static QVector<Slot> slotsFor(Layout kind);

    void rebuildLayout();
    void addTileAt(VideoTile *tile, int index);
    void selectIndex(int index);
    void emitActive();
    int indexOf(const zp::ChannelRef &ch) const;
    int firstEmptyIndex() const;
    VideoTile *createTile(const zp::ChannelRef &ch, const QString &name, bool ptz);
    void updateEmptyState();
    QWidget *createEmptySlot(int index);   // 空窗格占位（细边框 + 提示）

    Layout m_kind = L2x2;
    int m_activeIndex = -1;
    QVector<VideoTile *> m_tiles;
    VideoSettings m_settings;
    AnalysisService *m_analysis = nullptr;
    IFrameAnalyzer *m_ocrAnalyzer = nullptr;
    QStringList m_classNames;
    bool m_captureEnabled = false;

    QStackedLayout *m_stack = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_grid = nullptr;
    QWidget *m_empty = nullptr;
    QVector<QWidget *> m_emptySlots;   // 与 m_tiles 对齐的空槽占位
};

} // namespace zp

#endif // ZP_VIDEO_GRID_H
