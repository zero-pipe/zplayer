#include "player/video_grid.h"
#include "player/video_tile.h"

#include <QGridLayout>
#include <QLayoutItem>

namespace zp {

VideoGrid::VideoGrid(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(3);
    m_tiles.resize(m_rows * m_cols);
    setStyleSheet(QStringLiteral("VideoGrid{background:#111820;}"));
}

VideoGrid::~VideoGrid()
{
    for (VideoTile *tile : m_tiles) {
        if (tile) {
            tile->stop();
        }
    }
}

void VideoGrid::setGridSize(int rows, int cols)
{
    rows = qBound(1, rows, 8);
    cols = qBound(1, cols, 8);
    if (rows == m_rows && cols == m_cols) {
        return;
    }

    const int newCount = rows * cols;
    QVector<VideoTile *> next(newCount, nullptr);

    int slot = 0;
    for (VideoTile *tile : m_tiles) {
        if (!tile) {
            continue;
        }
        if (slot < newCount) {
            next[slot] = tile;
            ++slot;
        } else {
            tile->stop();
            tile->deleteLater();
        }
    }

    m_tiles = next;
    m_rows = rows;
    m_cols = cols;
    if (m_activeIndex >= m_tiles.size() || !m_tiles.value(m_activeIndex)) {
        m_activeIndex = -1;
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) {
                m_activeIndex = i;
                break;
            }
        }
    }

    relayout();
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles[i]) {
            m_tiles[i]->setSelected(i == m_activeIndex);
        }
    }
    emitActive();
}

void VideoGrid::openStream(const QString &deviceId, const QString &name, bool ptzCapable, const QUrl &url)
{
    int index = indexOf(deviceId);
    if (index < 0) {
        index = firstEmptyIndex();
        if (index < 0) {
            index = m_activeIndex >= 0 ? m_activeIndex : 0;
            if (VideoTile *old = m_tiles.value(index)) {
                old->stop();
                old->deleteLater();
                m_tiles[index] = nullptr;
            }
        }

        auto *tile = new VideoTile(deviceId, name, ptzCapable, this);
        connect(tile, &VideoTile::clickedTile, this, [this](const QString &id) {
            const int idx = indexOf(id);
            if (idx >= 0) {
                selectIndex(idx);
            }
        });
        connect(tile, &VideoTile::closeRequested, this, [this](const QString &id) {
            closeStream(id);
        });
        m_tiles[index] = tile;
        m_layout->addWidget(tile, index / m_cols, index % m_cols);
    }

    selectIndex(index);
    if (VideoTile *tile = m_tiles.value(index)) {
        tile->play(url);
    }
}

void VideoGrid::closeStream(const QString &deviceId)
{
    const int index = indexOf(deviceId);
    if (index < 0) {
        return;
    }
    VideoTile *tile = m_tiles[index];
    m_tiles[index] = nullptr;
    if (tile) {
        tile->stop();
        tile->deleteLater();
    }
    if (m_activeIndex == index) {
        m_activeIndex = -1;
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) {
                m_activeIndex = i;
                break;
            }
        }
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) {
                m_tiles[i]->setSelected(i == m_activeIndex);
            }
        }
        emitActive();
    }
}

void VideoGrid::relayout()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        delete item;
    }
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles[i]) {
            m_layout->addWidget(m_tiles[i], i / m_cols, i % m_cols);
        }
    }
}

void VideoGrid::selectIndex(int index)
{
    if (index < 0 || index >= m_tiles.size() || !m_tiles.value(index)) {
        return;
    }
    if (m_activeIndex >= 0 && m_activeIndex < m_tiles.size() && m_tiles[m_activeIndex]) {
        m_tiles[m_activeIndex]->setSelected(false);
    }
    m_activeIndex = index;
    m_tiles[index]->setSelected(true);
    emitActive();
}

int VideoGrid::indexOf(const QString &deviceId) const
{
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles[i] && m_tiles[i]->deviceId() == deviceId) {
            return i;
        }
    }
    return -1;
}

int VideoGrid::firstEmptyIndex() const
{
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (!m_tiles[i]) {
            return i;
        }
    }
    return -1;
}

void VideoGrid::emitActive()
{
    if (m_activeIndex >= 0 && m_activeIndex < m_tiles.size() && m_tiles[m_activeIndex]) {
        VideoTile *tile = m_tiles[m_activeIndex];
        emit activeDeviceChanged(tile->deviceId(), tile->ptzCapable());
    } else {
        emit activeDeviceChanged(QString(), false);
    }
}

} // namespace zp
