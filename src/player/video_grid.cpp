#include "player/video_grid.h"
#include "player/video_tile.h"
#include "analysis/analysis_service.h"
#include "ui/icons.h"

#include <QGridLayout>
#include <QLabel>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QLayoutItem>

namespace zp {

void VideoGrid::setClassNames(const QStringList &names)
{
    m_classNames = names;
    for (VideoTile *t : m_tiles) {
        if (t) {
            t->setClassNames(names);
        }
    }
}

void VideoGrid::setCaptureEnabled(bool on)
{
    m_captureEnabled = on;
    for (VideoTile *t : m_tiles) {
        if (t) {
            t->setCaptureEnabled(on);
        }
    }
}

void VideoGrid::setAnalysisService(AnalysisService *service)
{
    m_analysis = service;
    if (!service) {
        return;
    }
    // Deliver each result to the tile that owns the source channel.
    connect(service, &AnalysisService::resultReady, this,
            [this](const zp::ChannelRef &ref, const zp::AnalysisResult &result) {
                const int idx = indexOf(ref);
                if (idx >= 0 && m_tiles.value(idx)) {
                    m_tiles[idx]->setAnalysisResult(result);
                }
            });
    for (VideoTile *t : m_tiles) {
        if (t && t->player()) {
            service->attach(t->player(), t->channel());
        }
    }
}

VideoTile *VideoGrid::tileFor(const zp::ChannelRef &ch) const
{
    const int idx = indexOf(ch);
    return idx >= 0 ? m_tiles.value(idx) : nullptr;
}

VideoTile *VideoGrid::activeTile() const
{
    if (m_activeIndex >= 0 && m_activeIndex < m_tiles.size()) {
        return m_tiles.value(m_activeIndex);
    }
    return nullptr;
}

VideoGrid::VideoGrid(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("VideoArea"));

    m_empty = new QWidget(this);
    m_empty->setObjectName(QStringLiteral("GridEmpty"));
    auto *ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto *icon = new QLabel(m_empty);
    icon->setPixmap(icons::cameraPixmap(QColor(0x2E, 0x3A, 0x49), 56));
    icon->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(tr("从左侧目录选择通道开始预览"), m_empty);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color:#5F6B7A;font-size:14px;"));
    ev->addWidget(icon);
    ev->addSpacing(10);
    ev->addWidget(hint);
    ev->addStretch(1);

    m_gridHost = new QWidget(this);
    m_gridHost->setObjectName(QStringLiteral("GridHost"));
    m_gridHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(3);   // gaps show the grid-host color as separator lines

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_gridHost);

    // Allocate the slot vector for the default layout before first use.
    m_tiles.resize(slotsFor(m_kind).size());
    rebuildLayout();
    updateEmptyState();
}

VideoGrid::~VideoGrid()
{
    for (VideoTile *t : m_tiles) {
        if (t) {
            t->stop();
        }
    }
}

QVector<VideoGrid::Slot> VideoGrid::slotsFor(Layout kind)
{
    QVector<Slot> s;
    switch (kind) {
    case L1x1:
        s.append({0, 0, 1, 1});
        break;
    case L2x2:
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                s.append({r, c, 1, 1});
        break;
    case L3x3:
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                s.append({r, c, 1, 1});
        break;
    case L4x4:
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                s.append({r, c, 1, 1});
        break;
    case L1p5:
        // 3 columns x 3 rows grid: big tile spans col0..1, rows0..1;
        // five small tiles fill col2 rows0..1 and row2 cols0..2.
        s.append({0, 0, 2, 2}); // big
        s.append({0, 2, 1, 1});
        s.append({1, 2, 1, 1});
        s.append({2, 0, 1, 1});
        s.append({2, 1, 1, 1});
        s.append({2, 2, 1, 1});
        break;
    }
    return s;
}

void VideoGrid::setLayoutKind(Layout kind)
{
    if (kind == m_kind) {
        return;
    }
    const int newCount = slotsFor(kind).size();
    const int oldCount = m_tiles.size();

    // Keep existing tiles that fit; stop the overflow.
    if (newCount < oldCount) {
        for (int i = newCount; i < oldCount; ++i) {
            VideoTile *t = m_tiles.value(i);
            if (t) {
                t->stop();
                emit streamClosed(t->channel());
                t->deleteLater();
            }
        }
        m_tiles.resize(newCount);
        m_emptySlots.resize(newCount);
    } else if (newCount > m_tiles.size()) {
        m_tiles.resize(newCount);
        m_emptySlots.resize(newCount);
    }

    m_kind = kind;
    if (m_activeIndex >= m_tiles.size() || !m_tiles.value(m_activeIndex)) {
        m_activeIndex = -1;
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) { m_activeIndex = i; break; }
        }
    }

    rebuildLayout();
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles[i]) {
            m_tiles[i]->setSelected(i == m_activeIndex);
        }
    }
    updateEmptyState();
    emit layoutChanged(static_cast<int>(kind));
    emitActive();
}

QWidget *VideoGrid::createEmptySlot(int index)
{
    auto *w = new QWidget(m_gridHost);
    w->setObjectName(QStringLiteral("GridEmptySlot"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(8);
    vl->addStretch(1);

    auto *icon = new QLabel(w);
    icon->setPixmap(icons::cameraPixmap(QColor(0x2E, 0x3A, 0x49), 40));
    icon->setAlignment(Qt::AlignCenter);
    vl->addWidget(icon);

    auto *hint = new QLabel(tr("窗格 #%1 · 从左侧目录选择").arg(index + 1), w);
    hint->setObjectName(QStringLiteral("GridEmptySlotHint"));
    hint->setAlignment(Qt::AlignCenter);
    vl->addWidget(hint);
    vl->addStretch(1);
    return w;
}

void VideoGrid::rebuildLayout()
{
    // Detach all widgets from the grid without destroying them.
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(m_gridHost); // keep alive, just unmanaged
            w->hide();
        }
        delete item;
    }

    // 确保每个槽位都有一个占位或真实 tile，切换分屏时先把画面框全部画出来。
    m_emptySlots.resize(m_tiles.size());
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (!m_tiles[i] && !m_emptySlots[i]) {
            m_emptySlots[i] = createEmptySlot(i);
        }
    }

    const QVector<Slot> layoutSlots = slotsFor(m_kind);
    for (int i = 0; i < layoutSlots.size(); ++i) {
        const Slot &s = layoutSlots[i];
        if (i >= m_tiles.size()) {
            continue;
        }
        QWidget *w = m_tiles[i] ? static_cast<QWidget *>(m_tiles[i])
                                : static_cast<QWidget *>(m_emptySlots.value(i));
        if (w) {
            m_grid->addWidget(w, s.row, s.col, s.rowSpan, s.colSpan);
            w->show();
        }
    }
    // Normalize column/row stretch. Reset any stale stretch from a previous
    // layout first (e.g. 1+5 sets 3 rows; switching to 2x2 must clear row 2).
    for (int c = 0; c < 8; ++c) {
        m_grid->setColumnStretch(c, 0);
    }
    for (int r = 0; r < 8; ++r) {
        m_grid->setRowStretch(r, 0);
    }
    int maxCol = 0, maxRow = 0;
    for (const Slot &s : layoutSlots) {
        maxCol = qMax(maxCol, s.col + s.colSpan);
        maxRow = qMax(maxRow, s.row + s.rowSpan);
    }
    for (int c = 0; c < maxCol; ++c) {
        m_grid->setColumnStretch(c, (m_kind == L1p5 && c < 2) ? 2 : 1);
    }
    for (int r = 0; r < maxRow; ++r) {
        m_grid->setRowStretch(r, (m_kind == L1p5 && r < 2) ? 2 : 1);
    }
}

VideoTile *VideoGrid::createTile(const ChannelRef &ch, const QString &name, bool ptz)
{
    auto *tile = new VideoTile(ch, name, ptz, m_gridHost);
    tile->applyVideoSettings(m_settings);  // inherit the grid's defaults
    tile->setClassNames(m_classNames);     // detector class list (may be empty)
    tile->setCaptureEnabled(m_captureEnabled);
    connect(tile, &VideoTile::captureSettingChanged, this, &VideoGrid::captureSettingChanged);
    connect(tile, &VideoTile::sampleCaptured, this, &VideoGrid::sampleCaptured);
    connect(tile, &VideoTile::clickedTile, this,
            [this](const zp::ChannelRef &c) {
                const int idx = indexOf(c);
                if (idx >= 0) {
                    selectIndex(idx);
                }
            });
    connect(tile, &VideoTile::closeRequested, this, &VideoGrid::closeStream);
    connect(tile, &VideoTile::retryRequested, this, &VideoGrid::retryRequested);
    connect(tile, &VideoTile::maximizeRequested, this, &VideoGrid::maximizeRequested);
    connect(tile, &VideoTile::snapshotTaken, this, &VideoGrid::snapshotTaken);
    connect(tile, &VideoTile::evidenceExportRequested, this,
            &VideoGrid::evidenceExportRequested);
    connect(tile, &VideoTile::scanFinished, this, &VideoGrid::streamScanFinished);
    connect(tile, &VideoTile::statusMessage, this, &VideoGrid::tileStatus);
    connect(tile, &VideoTile::playbackStateChanged, this,
            &VideoGrid::tilePlaybackStateChanged);

    // ROI / OCR are per-tile analysis controls; forward them to the service
    // with the tile's player id.
    connect(tile, &VideoTile::roiChanged, this,
            [this](const zp::ChannelRef &ch, const QRectF &roi) {
                if (!m_analysis) {
                    return;
                }
                if (VideoTile *t = tileFor(ch)) {
                    if (IMediaPlayer *p = t->player()) {
                        m_analysis->setRoi(p->playerId(), roi);
                    }
                }
            });
    connect(tile, &VideoTile::ocrToggled, this,
            [this](const zp::ChannelRef &ch, bool on) {
                if (!m_analysis) {
                    return;
                }
                if (VideoTile *t = tileFor(ch)) {
                    if (IMediaPlayer *p = t->player()) {
                        m_analysis->setSourceAnalyzer(p->playerId(),
                                                      on ? m_ocrAnalyzer : nullptr);
                    }
                }
            });

    // Route this tile's frames to the shared analysis service. Bookkeeping is
    // released from destroyed() rather than an explicit delete path: tiles are
    // removed via deleteLater() from several places.
    if (m_analysis) {
        if (IMediaPlayer *player = tile->player()) {
            m_analysis->attach(player, ch);
            const quint64 playerId = player->playerId();
            // Capture the service (not `this`): the grid is destroyed before
            // the service, and this connection outlives the grid.
            AnalysisService *svc = m_analysis;
            connect(tile, &QObject::destroyed, svc, [svc, playerId]() {
                svc->forget(playerId);
            });
        }
    }
    return tile;
}

void VideoGrid::addTileAt(VideoTile *tile, int index)
{
    if (index >= m_tiles.size()) {
        m_tiles.resize(index + 1);
    }
    m_tiles[index] = tile;
    rebuildLayout();
}

void VideoGrid::setVideoSettings(const VideoSettings &settings)
{
    m_settings = settings;
    for (VideoTile *t : m_tiles) {
        if (t) {
            t->applyVideoSettings(settings);
        }
    }
}

void VideoGrid::openStream(const ChannelRef &ch, const QString &name,
                           bool ptzCapable, const QUrl &url)
{
    int index = indexOf(ch);
    if (index < 0) {
        index = firstEmptyIndex();
        if (index < 0) {
            // Layout is full: reuse the active (or last) slot.
            index = m_activeIndex >= 0 ? m_activeIndex : m_tiles.size() - 1;
            if (VideoTile *old = m_tiles.value(index)) {
                old->stop();
                emit streamClosed(old->channel());
                old->deleteLater();
                m_tiles[index] = nullptr;
            }
        }
        if (index < 0) {
            return;
        }
        VideoTile *tile = createTile(ch, name, ptzCapable);
        addTileAt(tile, index);
    }

    selectIndex(index);
    if (VideoTile *tile = m_tiles.value(index)) {
        tile->play(url);
    }
    updateEmptyState();
}

void VideoGrid::closeStream(const ChannelRef &ch)
{
    const int index = indexOf(ch);
    if (index < 0) {
        return;
    }
    VideoTile *tile = m_tiles[index];
    m_tiles[index] = nullptr;
    if (tile) {
        tile->stop();
        tile->deleteLater();
    }
    rebuildLayout();
    emit streamClosed(ch);
    if (m_activeIndex == index) {
        m_activeIndex = -1;
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) { m_activeIndex = i; break; }
        }
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]) {
                m_tiles[i]->setSelected(i == m_activeIndex);
            }
        }
    }
    updateEmptyState();
    emitActive();
}

void VideoGrid::setSingleChannel(const ChannelRef &ch)
{
    const int index = indexOf(ch);
    if (index < 0) {
        return;
    }
    // Move the tile to slot 0, drop others, switch to 1x1, reopen.
    VideoTile *keep = m_tiles[index];
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (i != index && m_tiles[i]) {
            m_tiles[i]->stop();
            emit streamClosed(m_tiles[i]->channel());
            m_tiles[i]->deleteLater();
        }
    }
    m_tiles.clear();
    m_kind = L1x1;
    m_tiles.append(keep);
    m_activeIndex = 0;
    rebuildLayout();
    keep->setSelected(true);
    updateEmptyState();
    emit layoutChanged(static_cast<int>(L1x1));
    emitActive();
}

void VideoGrid::focusChannel(const ChannelRef &ch)
{
    const int index = indexOf(ch);
    if (index >= 0) {
        selectIndex(index);
    }
}

void VideoGrid::showLoadingPlaceholder(const ChannelRef &ch, const QString &name)
{
    if (indexOf(ch) >= 0) {
        selectIndex(indexOf(ch));
        return;
    }
    int index = firstEmptyIndex();
    if (index < 0) {
        index = m_activeIndex >= 0 ? m_activeIndex : m_tiles.size() - 1;
        if (VideoTile *old = m_tiles.value(index)) {
            old->stop();
            emit streamClosed(old->channel());
            old->deleteLater();
            m_tiles[index] = nullptr;
        }
    }
    if (index < 0) {
        return;
    }
    VideoTile *tile = createTile(ch, name, true);
    tile->showConnecting(tr("连接中…"));
    addTileAt(tile, index);
    selectIndex(index);
    updateEmptyState();
}

void VideoGrid::markFailed(const ChannelRef &ch, const QString &message)
{
    int index = indexOf(ch);
    if (index < 0) {
        // Placeholder may have been dropped by a layout change; recreate.
        return;
    }
    if (VideoTile *tile = m_tiles.value(index)) {
        tile->showFailed(message);
    }
}

void VideoGrid::markConnecting(const ChannelRef &ch)
{
    const int index = indexOf(ch);
    if (index >= 0) {
        if (VideoTile *tile = m_tiles.value(index)) {
            tile->showConnecting(tr("连接中…"));
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

void VideoGrid::emitActive()
{
    if (m_activeIndex >= 0 && m_activeIndex < m_tiles.size() && m_tiles[m_activeIndex]) {
        VideoTile *t = m_tiles[m_activeIndex];
        emit activeChannelChanged(t->channel(), t->name(), t->ptzCapable());
    } else {
        emit activeChannelChanged(ChannelRef(), QString(), false);
    }
}

int VideoGrid::indexOf(const ChannelRef &ch) const
{
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles[i] && m_tiles[i]->channel() == ch) {
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

bool VideoGrid::contains(const ChannelRef &ch) const
{
    return indexOf(ch) >= 0;
}

int VideoGrid::tileCount() const
{
    int n = 0;
    for (VideoTile *t : m_tiles) {
        if (t) ++n;
    }
    return n;
}

void VideoGrid::stopAll()
{
    for (VideoTile *t : m_tiles) {
        if (t) {
            t->stop();
        }
    }
}

void VideoGrid::updateEmptyState()
{
    // 始终显示网格（空槽自带占位框）；整页 m_empty 不再使用。
    m_stack->setCurrentWidget(m_gridHost);
}

} // namespace zp
