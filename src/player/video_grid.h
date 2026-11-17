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

signals:
    void activeChannelChanged(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void streamClosed(const zp::ChannelRef &ch);
    void retryRequested(const zp::ChannelRef &ch);
    void maximizeRequested(const zp::ChannelRef &ch);
    void layoutChanged(int index);

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

    Layout m_kind = L2x2;
    int m_activeIndex = -1;
    QVector<VideoTile *> m_tiles;
    VideoSettings m_settings;

    QStackedLayout *m_stack = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_grid = nullptr;
    QWidget *m_empty = nullptr;
};

} // namespace zp

#endif // ZP_VIDEO_GRID_H
