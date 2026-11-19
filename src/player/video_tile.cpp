#include "player/video_tile.h"
#include "player/media_factory.h"
#include "ui/analysis_overlay.h"
#include "ui/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QRegExp>

namespace zp {

VideoTile::VideoTile(const ChannelRef &ch, const QString &name, bool ptzCapable,
                     QWidget *parent)
    : QWidget(parent)
    , m_channel(ch)
    , m_name(name)
    , m_ptzCapable(ptzCapable)
{
    setObjectName(QStringLiteral("VideoTile"));
    setProperty("selected", false);
    setMinimumSize(120, 76);

    // --- Header (floats over the video) ---
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("TileHeader"));

    m_title = new QLabel(name, header);
    m_title->setObjectName(QStringLiteral("TileTitle"));
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_status = new QLabel(header);
    m_status->setObjectName(QStringLiteral("TileStatus"));

    auto *maxBtn = new QToolButton(header);
    maxBtn->setObjectName(QStringLiteral("TileBtn"));
    maxBtn->setText(QString::fromUtf8("⛶"));
    maxBtn->setToolTip(tr("单画面"));
    maxBtn->setCursor(Qt::PointingHandCursor);
    connect(maxBtn, &QToolButton::clicked, this, [this]() {
        emit maximizeRequested(m_channel);
    });

    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("TileBtn"));
    closeBtn->setProperty("role", QStringLiteral("close"));
    closeBtn->setText(QString::fromUtf8("✕"));
    closeBtn->setToolTip(tr("关闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, [this]() {
        emit closeRequested(m_channel);
    });

    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 6, 8, 8);
    hl->setSpacing(8);
    hl->addWidget(m_title, 1);
    hl->addWidget(m_status);
    hl->addWidget(maxBtn);
    hl->addWidget(closeBtn);

    // --- Stage: video page + overlay page ---
    m_player = media::createPlayer(MediaBackend::Auto);
    m_player->setParent(this);
    m_video = m_player->renderer()->widget();
    if (m_video) {
        m_video->setObjectName(QStringLiteral("TileVideo"));
        m_video->setMinimumSize(80, 45);
    }

    m_overlay = new QWidget(this);
    m_overlay->setObjectName(QStringLiteral("TileOverlay"));
    auto *ov = new QVBoxLayout(m_overlay);
    ov->setContentsMargins(12, 12, 12, 12);
    ov->addStretch(1);
    m_overlayText = new QLabel(tr("未连接"), m_overlay);
    m_overlayText->setObjectName(QStringLiteral("OverlayText"));
    m_overlayText->setAlignment(Qt::AlignCenter);
    m_overlayText->setWordWrap(true);
    ov->addWidget(m_overlayText);
    m_retryBtn = new QPushButton(tr("重试"), m_overlay);
    m_retryBtn->setObjectName(QStringLiteral("OverlayRetry"));
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->hide();
    connect(m_retryBtn, &QPushButton::clicked, this, [this]() {
        if (m_state == Failed) {
            emit retryRequested(m_channel);
        }
    });
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    btnRow->addWidget(m_retryBtn);
    btnRow->addStretch(1);
    ov->addLayout(btnRow);
    ov->addStretch(1);

    auto *stageWidget = new QWidget(this);
    m_stage = new QStackedLayout(stageWidget);
    m_stage->setContentsMargins(0, 0, 0, 0);
    if (m_video) {
        m_stage->addWidget(m_video);
    }
    m_stage->addWidget(m_overlay);

    // Detection overlay sits directly on the video surface. Boxes are
    // normalized to the full tile frame (bars included), so it simply fills the
    // video widget.
    if (m_video) {
        m_aiOverlay = new AnalysisOverlay(m_video);
        m_aiOverlay->setGeometry(m_video->rect());
        m_aiOverlay->raise();
        m_video->installEventFilter(this);
    }

    // --- Footer: bottom action bar (snapshot now, AI actions later) ---
    m_footer = new QWidget(this);
    m_footer->setObjectName(QStringLiteral("TileFooter"));
    auto *fl = new QHBoxLayout(m_footer);
    fl->setContentsMargins(8, 3, 8, 3);
    fl->setSpacing(6);
    auto *snapBtn = new QToolButton(m_footer);
    snapBtn->setObjectName(QStringLiteral("TileBtn"));
    snapBtn->setIcon(QIcon(icons::camera(true)));
    snapBtn->setIconSize(QSize(15, 15));
    snapBtn->setToolTip(tr("抓图"));
    snapBtn->setCursor(Qt::PointingHandCursor);
    connect(snapBtn, &QToolButton::clicked, this, &VideoTile::takeSnapshot);

    // Local / recorded playback gets a transport bar; live streams keep only
    // the action buttons (they have no meaningful timeline).
    if (m_player && m_player->supportsPlaybackControl()) {
        buildTransport(fl);
    } else {
        // Right-align the actions (matches common OS/desktop placement).
        fl->addStretch(1);
    }
    fl->addWidget(snapBtn);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(1, 1, 1, 1);
    vl->setSpacing(0);
    vl->addWidget(header);
    vl->addWidget(stageWidget, 1);
    vl->addWidget(m_footer);

    connect(m_player, &IMediaPlayer::started, this, [this]() {
        setState(Playing, tr("播放中"));
        m_stage->setCurrentWidget(m_video ? m_video : m_overlay);
    });
    connect(m_player, &IMediaPlayer::stopped, this, [this]() {
        if (m_state != Failed) {
            setState(Idle, tr("已停止"));
        }
        if (m_aiOverlay) {
            m_aiOverlay->clear();
        }
    });
    connect(m_player, &IMediaPlayer::failed, this, [this](const QString &msg) {
        setState(Failed, tr("播放失败"));
        m_overlayText->setToolTip(msg);
    });

    setState(Idle, tr("未连接"));
    refreshSelectionStyle();
}

VideoTile::~VideoTile()
{
    if (m_player) {
        m_player->stop();
    }
}

void VideoTile::play(const QUrl &url)
{
    if (!url.isValid()) {
        setState(Failed, tr("地址无效"));
        return;
    }
    setState(Connecting, tr("连接中…"));
    m_player->open(url);
    m_player->play();
}

void VideoTile::stop()
{
    if (m_player) {
        m_player->stop();
    }
}

void VideoTile::applyVideoSettings(const VideoSettings &settings)
{
    if (m_player) {
        m_player->setVideoSettings(settings);
    }
}

void VideoTile::takeSnapshot()
{
    if (!m_player || m_state != Playing) {
        emit snapshotTaken(m_channel, QString(), false);
        return;
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString safe = m_name.isEmpty() ? m_channel.channelId : m_name;
    safe.replace(QRegExp(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    const QString path = QDir(dir).filePath(
        QStringLiteral("%1_%2.png").arg(safe, stamp));
    const bool ok = m_player->grabSnapshot(path);
    emit snapshotTaken(m_channel, path, ok);
}

void VideoTile::showConnecting(const QString &text)
{
    setState(Connecting, text.isEmpty() ? tr("连接中…") : text);
}

void VideoTile::showFailed(const QString &text)
{
    setState(Failed, text.isEmpty() ? tr("播放失败") : text);
}

void VideoTile::setState(State state, const QString &text)
{
    m_state = state;
    QString color;
    switch (state) {
    case Connecting: color = QStringLiteral("#E3B341"); break;
    case Playing:    color = QStringLiteral("#2ECC71"); break;
    case Failed:     color = QStringLiteral("#F85149"); break;
    case Idle:
    default:         color = QStringLiteral("#9AA7B6"); break;
    }
    m_status->setText(QString::fromUtf8("● %1").arg(text));
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(color));

    m_overlayText->setText(text);
    m_retryBtn->setVisible(state == Failed);
    // Keep the video widget visible while connecting/playing so the engine's
    // OpenGL context is created early (libmpv needs its render context ready
    // before the first frame can be decoded).
    if (state == Playing || state == Connecting) {
        m_stage->setCurrentWidget(m_video ? m_video : m_overlay);
    } else {
        m_stage->setCurrentWidget(m_overlay);
    }
    updateTransportUi();
}

void VideoTile::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    refreshSelectionStyle();
}

void VideoTile::refreshSelectionStyle()
{
    setProperty("selected", m_selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void VideoTile::mousePressEvent(QMouseEvent *e)
{
    emit clickedTile(m_channel);
    QWidget::mousePressEvent(e);
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent *e)
{
    emit maximizeRequested(m_channel);
    QWidget::mouseDoubleClickEvent(e);
}

// ---------------------------------------------------------------------------
// Transport bar
// ---------------------------------------------------------------------------
QString VideoTile::formatTime(double seconds)
{
    if (seconds < 0) {
        return QStringLiteral("--:--");
    }
    const int t = int(seconds + 0.5);
    const int h = t / 3600;
    const int m = (t / 60) % 60;
    const int s = t % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

void VideoTile::buildTransport(QHBoxLayout *row)
{
    m_hasTransport = true;

    m_playBtn = new QToolButton(m_footer);
    m_playBtn->setObjectName(QStringLiteral("TileBtn"));
    m_playBtn->setText(QString::fromUtf8("▶"));
    m_playBtn->setToolTip(tr("播放 / 暂停"));
    m_playBtn->setCursor(Qt::PointingHandCursor);
    connect(m_playBtn, &QToolButton::clicked, this, [this]() {
        if (!m_player) {
            return;
        }
        if (m_player->state() == MediaState::Playing) {
            m_player->pause();
        } else {
            m_player->play();
        }
    });

    m_timeLabel = new QLabel(QStringLiteral("--:-- / --:--"), m_footer);
    m_timeLabel->setObjectName(QStringLiteral("TileTime"));

    m_posSlider = new QSlider(Qt::Horizontal, m_footer);
    m_posSlider->setObjectName(QStringLiteral("TileSeek"));
    m_posSlider->setRange(0, 0);
    m_posSlider->setMinimumWidth(40);
    m_posSlider->setCursor(Qt::PointingHandCursor);
    connect(m_posSlider, &QSlider::sliderPressed, this, [this]() { m_scrubbing = true; });
    connect(m_posSlider, &QSlider::sliderReleased, this, [this]() {
        m_scrubbing = false;
        if (m_player && m_duration > 0) {
            m_player->seek(m_posSlider->value() / 1000.0);
        }
    });
    connect(m_posSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_scrubbing && m_timeLabel) {
            m_timeLabel->setText(formatTime(v / 1000.0) + QStringLiteral(" / ")
                                 + formatTime(m_duration));
        }
    });

    m_speedBtn = new QToolButton(m_footer);
    m_speedBtn->setObjectName(QStringLiteral("TileBtn"));
    m_speedBtn->setText(QStringLiteral("1x"));
    m_speedBtn->setToolTip(tr("播放速度"));
    m_speedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_speedBtn, &QToolButton::clicked, this, &VideoTile::cycleSpeed);

    m_aiBtn = new QToolButton(m_footer);
    m_aiBtn->setObjectName(QStringLiteral("TileBtn"));
    m_aiBtn->setText(QStringLiteral("AI"));
    m_aiBtn->setCheckable(true);
    m_aiBtn->setToolTip(tr("AI 跳过无人片段（快放）"));
    m_aiBtn->setCursor(Qt::PointingHandCursor);
    connect(m_aiBtn, &QToolButton::toggled, this, &VideoTile::setAiSkipEnabled);

    m_evidenceBtn = new QToolButton(m_footer);
    m_evidenceBtn->setObjectName(QStringLiteral("TileBtn"));
    m_evidenceBtn->setText(tr("证据"));
    m_evidenceBtn->setToolTip(tr("导出检测事件证据（报告 + 截图）"));
    m_evidenceBtn->setCursor(Qt::PointingHandCursor);
    connect(m_evidenceBtn, &QToolButton::clicked, this, [this]() {
        emit evidenceExportRequested(m_channel);
    });

    row->addWidget(m_playBtn);
    row->addWidget(m_timeLabel);
    row->addWidget(m_posSlider, 1);
    row->addWidget(m_speedBtn);
    row->addWidget(m_aiBtn);
    row->addWidget(m_evidenceBtn);

    connect(m_player, &IMediaPlayer::positionChanged, this, [this](double pos) {
        if (!m_scrubbing && m_duration > 0) {
            if (m_posSlider) {
                m_posSlider->setValue(int(pos * 1000.0));
            }
            if (m_timeLabel) {
                m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ")
                                     + formatTime(m_duration));
            }
        }
    });
    connect(m_player, &IMediaPlayer::durationChanged, this, [this](double d) {
        m_duration = d;
        if (m_posSlider) {
            m_posSlider->setRange(0, d > 0 ? int(d * 1000.0) : 0);
        }
        if (m_timeLabel) {
            m_timeLabel->setText(formatTime(m_player ? m_player->position() : 0)
                                 + QStringLiteral(" / ") + formatTime(m_duration));
        }
    });
    connect(m_player, &IMediaPlayer::speedChanged, this, [this](double s) {
        if (m_speedBtn) {
            m_speedBtn->setText(QStringLiteral("%1x").arg(s, 0, 'g', 2));
        }
    });
}

void VideoTile::cycleSpeed()
{
    static const double speeds[] = {1.0, 2.0, 4.0, 8.0, 16.0};
    m_speedIndex = (m_speedIndex + 1) % int(sizeof(speeds) / sizeof(speeds[0]));
    if (m_player) {
        m_player->setSpeed(speeds[m_speedIndex]);
    }
}

void VideoTile::updateTransportUi()
{
    if (m_playBtn) {
        m_playBtn->setText(m_state == Playing ? QString::fromUtf8("❚❚")
                                              : QString::fromUtf8("▶"));
    }
}

// ---------------------------------------------------------------------------
// AI overlay
// ---------------------------------------------------------------------------
void VideoTile::setAnalysisResult(const zp::AnalysisResult &result)
{
    if (m_aiOverlay) {
        m_aiOverlay->setBoxes(result.boxes, result.videoRect);
    }
    updateAiSkip(result.pts, result.boxes);
    updateEvidence(result.pts, result.boxes);
}

void VideoTile::setAiSkipEnabled(bool on)
{
    m_aiSkip = on;
    m_lastPresenceMs = 0;
    m_lastSeekMs = 0;
    if (m_aiBtn && m_aiBtn->isChecked() != on) {
        m_aiBtn->setChecked(on);
    }
}

void VideoTile::updateAiSkip(double pts, const QVector<AnalysisBox> &boxes)
{
    if (!m_aiSkip || !m_player || !m_player->supportsPlaybackControl()) {
        return;
    }
    if (m_player->state() != MediaState::Playing) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    bool personPresent = false;
    for (const AnalysisBox &b : boxes) {
        if (b.classId == 0 /* person */ && b.score >= 0.25f) {
            personPresent = true;
            break;
        }
    }
    if (personPresent) {
        m_lastPresenceMs = now;
        return;
    }
    if (m_lastPresenceMs == 0) {
        m_lastPresenceMs = now;
        return;
    }

    static const int graceMs = []() {
        const int v = qEnvironmentVariableIntValue("ZP_AI_GRACE_MS");
        return v > 0 ? v : 3000;
    }();
    static const int cooldownMs = 1200;
    static const double jumpSec = 10.0;
    if (now - m_lastPresenceMs < graceMs || now - m_lastSeekMs < cooldownMs) {
        return;
    }

    const double pos = m_player->position();
    const double dur = m_player->duration();
    if (pos < 0 || dur <= 0) {
        return;
    }
    const double target = pos + jumpSec;
    if (target >= dur - 0.5) {
        return;   // near the end; let it play out
    }
    m_lastSeekMs = now;
    m_lastPresenceMs = now;
    closeOpenEvidence(pos, true);
    m_player->seek(target);
}

// ---------------------------------------------------------------------------
// Evidence accumulation
// ---------------------------------------------------------------------------
namespace {

bool isSalientClass(int classId)
{
    // Person, vehicles (0..8) and animals (14..23). Furniture noise is skipped.
    return (classId >= 0 && classId <= 8) || (classId >= 14 && classId <= 23);
}

} // namespace

QString VideoTile::captureEvidenceThumbnail()
{
    if (!m_player) {
        return QString();
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) {
        return QString();
    }
    const QString name = QStringLiteral("zp_ev_%1_%2.jpg")
                             .arg(m_player->playerId())
                             .arg(QDateTime::currentMSecsSinceEpoch());
    const QString path = QDir(dir).filePath(name);
    if (m_player->grabSnapshot(path)) {
        return path;
    }
    return QString();
}

void VideoTile::updateEvidence(double pts, const QVector<AnalysisBox> &boxes)
{
    if (pts < 0) {
        return;
    }
    // A seek breaks continuity; finish every open span at the new position.
    if (m_lastPts >= 0 && pts < m_lastPts - 0.5) {
        closeOpenEvidence(pts, true);
    }
    m_lastPts = pts;

    QHash<int, int> counts;
    QHash<int, QString> labels;
    for (const AnalysisBox &b : boxes) {
        if (!isSalientClass(b.classId)) {
            continue;
        }
        counts[b.classId] += 1;
        if (!labels.contains(b.classId) || b.score > 0.f) {
            labels[b.classId] = b.label;
        }
    }

    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        const int cls = it.key();
        if (m_openEvents.contains(cls)) {
            EvidenceEvent &ev = m_evidence[m_openEvents.value(cls)];
            ev.end = pts;
            ev.peakCount = qMax(ev.peakCount, it.value());
        } else if (m_evidence.size() < 5000) {
            EvidenceEvent ev;
            ev.start = pts;
            ev.end = pts;
            ev.classId = cls;
            ev.label = labels.value(cls);
            ev.peakCount = it.value();
            ev.thumbSource = captureEvidenceThumbnail();
            m_evidence.append(ev);
            m_openEvents.insert(cls, m_evidence.size() - 1);
        }
    }

    // Close spans whose class has been absent for more than 2 seconds.
    QHash<int, int> still;
    for (auto it = m_openEvents.constBegin(); it != m_openEvents.constEnd(); ++it) {
        const int idx = it.value();
        const EvidenceEvent &ev = m_evidence.at(idx);
        if (!counts.contains(it.key()) && pts - ev.end > 2.0) {
            continue;   // closed
        }
        still.insert(it.key(), idx);
    }
    m_openEvents = still;
}

void VideoTile::closeOpenEvidence(double atPts, bool all)
{
    if (all) {
        m_openEvents.clear();
        return;
    }
    QHash<int, int> still;
    for (auto it = m_openEvents.constBegin(); it != m_openEvents.constEnd(); ++it) {
        const EvidenceEvent &ev = m_evidence.at(it.value());
        if (atPts >= 0 && atPts - ev.end > 2.0) {
            continue;
        }
        still.insert(it.key(), it.value());
    }
    m_openEvents = still;
}

bool VideoTile::exportEvidence(const QString &outDir, QString *error)
{
    closeOpenEvidence(m_player ? m_player->position() : -1.0, true);
    const QString media = m_name.isEmpty() ? m_channel.channelId : m_name;
    return EvidenceExporter::writeReport(media, m_evidence, outDir, error);
}

bool VideoTile::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_video && event->type() == QEvent::Resize && m_aiOverlay && m_video) {
        m_aiOverlay->setGeometry(m_video->rect());
        m_aiOverlay->raise();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace zp
