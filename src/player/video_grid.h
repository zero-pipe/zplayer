#ifndef ZP_VIDEO_GRID_H
#define ZP_VIDEO_GRID_H

#include <QWidget>
#include <QVector>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QGridLayout;
QT_END_NAMESPACE

namespace zp {

class VideoTile;

// Split-screen container: arranges VideoTile cells in a rows x cols grid.
class VideoGrid : public QWidget {
    Q_OBJECT
public:
    explicit VideoGrid(QWidget *parent = nullptr);
    ~VideoGrid() override;

    void setGridSize(int rows, int cols);
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    void openStream(const QString &deviceId, const QString &name, bool ptzCapable, const QUrl &url);
    void closeStream(const QString &deviceId);

signals:
    void activeDeviceChanged(const QString &deviceId, bool ptzCapable);

private:
    void relayout();
    void selectIndex(int index);
    int indexOf(const QString &deviceId) const;
    int firstEmptyIndex() const;
    void emitActive();

    int m_rows = 2;
    int m_cols = 2;
    int m_activeIndex = -1;
    QGridLayout *m_layout = nullptr;
    QVector<VideoTile *> m_tiles;
};

} // namespace zp

#endif // ZP_VIDEO_GRID_H
