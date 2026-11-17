#ifndef ZP_VIDEO_TILE_H
#define ZP_VIDEO_TILE_H

#include <QWidget>
#include <QUrl>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
class QStackedLayout;
class QPushButton;
QT_END_NAMESPACE

namespace QtAV {
class AVPlayer;
class VideoOutput;
}

namespace zp {

// One stream cell: an AVPlayer + renderer behind a status overlay, with a
// floating header (title, state, close/maximize).
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

    void play(const QUrl &url);
    void stop();
    void setSelected(bool selected);

    // Drive the overlay state without starting playback (used by the grid for
    // "connecting" placeholders and error marking before/after playStarted).
    void showConnecting(const QString &text = QString());
    void showFailed(const QString &text);

signals:
    void clickedTile(const zp::ChannelRef &ch);
    void closeRequested(const zp::ChannelRef &ch);
    void retryRequested(const zp::ChannelRef &ch);
    void maximizeRequested(const zp::ChannelRef &ch);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    void setState(State state, const QString &text);
    void refreshSelectionStyle();

    ChannelRef m_channel;
    QString m_name;
    bool m_ptzCapable = false;
    bool m_selected = false;
    State m_state = Idle;

    QtAV::AVPlayer *m_player = nullptr;
    QtAV::VideoOutput *m_output = nullptr;
    QWidget *m_video = nullptr;
    QStackedLayout *m_stage = nullptr;
    QWidget *m_overlay = nullptr;
    QLabel *m_overlayText = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_title = nullptr;
};

} // namespace zp

#endif // ZP_VIDEO_TILE_H
