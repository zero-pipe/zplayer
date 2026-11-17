#ifndef ZP_VIDEO_TILE_H
#define ZP_VIDEO_TILE_H

#include <QWidget>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace QtAV {
class AVPlayer;
class VideoOutput;
}

namespace zp {

// One stream cell: an AVPlayer + its renderer widget, with a small title bar.
class VideoTile : public QWidget {
    Q_OBJECT
public:
    VideoTile(const QString &deviceId, const QString &name, bool ptzCapable, QWidget *parent = nullptr);
    ~VideoTile() override;

    QString deviceId() const { return m_deviceId; }
    bool ptzCapable() const { return m_ptzCapable; }

    void play(const QUrl &url);
    void stop();

    void setSelected(bool selected);

signals:
    void clickedTile(const QString &deviceId);
    void closeRequested(const QString &deviceId);

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    void setStatus(const QString &text, const QString &color);

    QString m_deviceId;
    QString m_name;
    bool m_ptzCapable = false;

    QtAV::AVPlayer *m_player = nullptr;
    QtAV::VideoOutput *m_output = nullptr;
    QWidget *m_video = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_frame = nullptr;
};

} // namespace zp

#endif // ZP_VIDEO_TILE_H
