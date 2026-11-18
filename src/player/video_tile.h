#ifndef ZP_VIDEO_TILE_H
#define ZP_VIDEO_TILE_H

#include <QWidget>
#include <QUrl>
#include "platform/platform_api.h"
#include "player/media_player.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
class QStackedLayout;
class QPushButton;
QT_END_NAMESPACE

namespace zp {

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

signals:
    void clickedTile(const zp::ChannelRef &ch);
    void closeRequested(const zp::ChannelRef &ch);
    void retryRequested(const zp::ChannelRef &ch);
    void maximizeRequested(const zp::ChannelRef &ch);
    // Emitted after the footer's snapshot action. path is where it was saved.
    void snapshotTaken(const zp::ChannelRef &ch, const QString &path, bool ok);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    void setState(State state, const QString &text);
    void refreshSelectionStyle();
    void takeSnapshot();

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
};

} // namespace zp

#endif // ZP_VIDEO_TILE_H
