#include "player/video_tile.h"
#include "player/media_factory.h"
#include "player/shaders.h"
#include "ui/analysis_overlay.h"
#include "ui/icons.h"
#include "ui/timeline_slider.h"

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
#include <QDebug>
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

    // ROI: restrict AI analysis to the framed region (click, then drag on the
    // picture; click again to clear).
    m_roiBtn = new QToolButton(m_footer);
    m_roiBtn->setObjectName(QStringLiteral("TileBtn"));
    m_roiBtn->setText(QStringLiteral("ROI"));
    m_roiBtn->setCheckable(true);
    m_roiBtn->setToolTip(tr("圈选分析区域：点击后拖动框选，再点一次清除"));
    m_roiBtn->setCursor(Qt::PointingHandCursor);
    connect(m_roiBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (!checked) {
            m_roiMode = false;
            m_roi = QRectF();
            if (m_zoomBtn && m_zoomBtn->isChecked()) {
                m_zoomBtn->setChecked(false);   // restores the full picture
            }
            if (m_zoomBtn) {
                m_zoomBtn->setEnabled(false);
            }
            if (m_aiOverlay) {
                m_aiOverlay->setRoi(QRectF());
                m_aiOverlay->setRoiDraft(QRectF());
            }
            if (m_video) {
                m_video->unsetCursor();
            }
            emit roiChanged(m_channel, QRectF());
            return;
        }
        m_roiMode = true;
        if (m_video) {
            m_video->setCursor(Qt::CrossCursor);
        }
    });

    // OCR: recognize scene text on this tile (inside the ROI when one is set).
    m_ocrBtn = new QToolButton(m_footer);
    m_ocrBtn->setObjectName(QStringLiteral("TileBtn"));
    m_ocrBtn->setText(tr("文字"));
    m_ocrBtn->setCheckable(true);
    m_ocrBtn->setToolTip(tr("文字识别（需 OCR 模型；配合 ROI 可只识别局部）"));
    m_ocrBtn->setCursor(Qt::PointingHandCursor);
    connect(m_ocrBtn, &QToolButton::toggled, this, [this](bool on) {
        emit ocrToggled(m_channel, on);
    });

    fl->addWidget(snapBtn);
    fl->addWidget(m_roiBtn);
    fl->addWidget(m_ocrBtn);

    // Zoom to ROI: fills the tile with the framed region (local magnification,
    // the practical form of "local upscaling" for a video renderer).
    m_zoomBtn = new QToolButton(m_footer);
    m_zoomBtn->setObjectName(QStringLiteral("TileBtn"));
    m_zoomBtn->setText(tr("放大"));
    m_zoomBtn->setCheckable(true);
    m_zoomBtn->setEnabled(false);   // needs an ROI first
    m_zoomBtn->setToolTip(tr("放大到 ROI 区域（需先圈选）"));
    m_zoomBtn->setCursor(Qt::PointingHandCursor);
    connect(m_zoomBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!m_player) {
            return;
        }
        m_player->setVideoCrop(on ? m_roi : QRectF());
        // While zoomed the visible picture IS the ROI: analysis no longer needs
        // (and must not re-apply) the ROI crop, so hand it the whole frame.
        // The dashed ROI frame would also be misleading over the zoomed view.
        if (on) {
            if (m_aiOverlay) {
                m_aiOverlay->setRoi(QRectF());
            }
            emit roiChanged(m_channel, QRectF());
        } else {
            if (m_aiOverlay) {
                m_aiOverlay->setRoi(m_roi);
            }
            emit roiChanged(m_channel, m_roi);
        }
    });

    // GPU sharpening (applies to whatever is on screen, ROI zoom included).
    m_sharpenBtn = new QToolButton(m_footer);
    m_sharpenBtn->setObjectName(QStringLiteral("TileBtn"));
    m_sharpenBtn->setText(tr("超分"));
    m_sharpenBtn->setCheckable(true);
    m_sharpenBtn->setToolTip(tr("GPU 锐化（配合 ROI 放大观看细节）"));
    m_sharpenBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sharpenBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!m_player) {
            return;
        }
        m_player->setShader(on ? ensureSharpenShaderFile() : QString());
    });

    fl->addWidget(m_zoomBtn);
    fl->addWidget(m_sharpenBtn);

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
        // Reaching the end of the file during a scan is the normal finish.
        finishScan();
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

    m_posSlider = new TimelineSlider(Qt::Horizontal, m_footer);
    m_posSlider->setObjectName(QStringLiteral("TileSeek"));
    m_posSlider->setRange(0, 0);
    m_posSlider->setMinimumWidth(40);
    m_posSlider->setCursor(Qt::PointingHandCursor);
    // Clicking a person segment jumps there (see TimelineSlider).
    connect(m_posSlider, &TimelineSlider::segmentActivated, this, [this](double t) {
        if (m_player) {
            m_player->seek(t);
        }
    });
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

    m_scanBtn = new QToolButton(m_footer);
    m_scanBtn->setObjectName(QStringLiteral("TileBtn"));
    m_scanBtn->setText(tr("扫描"));
    m_scanBtn->setCheckable(true);
    m_scanBtn->setToolTip(tr("快速扫描全片，标出有人物的时间段（可点击跳转）"));
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (checked) {
            startScan();
        } else {
            cancelScan();
        }
    });

    row->addWidget(m_playBtn);
    row->addWidget(m_timeLabel);
    row->addWidget(m_posSlider, 1);
    row->addWidget(m_speedBtn);
    row->addWidget(m_aiBtn);
    row->addWidget(m_evidenceBtn);
    row->addWidget(m_scanBtn);

    connect(m_player, &IMediaPlayer::positionChanged, this, [this](double pos) {
        if (m_scanning && m_duration > 0) {
            const int pct = int(qBound(0.0, pos / m_duration, 1.0) * 100.0);
            if (m_timeLabel) {
                m_timeLabel->setText(tr("扫描中 %1%").arg(pct));
            }
            if (pos >= m_duration - 0.4) {
                finishScan();
            }
            return;
        }
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
            m_posSlider->setMediaDuration(d);
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

// ---------------------------------------------------------------------------
// Smart timeline: fast scan
// ---------------------------------------------------------------------------
void VideoTile::startScan()
{
    if (!m_player || !m_player->supportsPlaybackControl() || m_duration <= 0.0) {
        if (m_scanBtn) {
            m_scanBtn->setChecked(false);
        }
        return;
    }
    if (m_scanning) {
        return;
    }
    m_scanning = true;
    m_scanSamples.clear();
    if (m_posSlider) {
        m_posSlider->clearSegments();
    }
    m_scanPrevSpeed = m_player->speed();
    m_scanPrevMuted = m_player->muted();
    m_scanPrevPos = m_player->position();
    if (m_scanBtn) {
        m_scanBtn->setText(tr("停止"));
    }
    // Scan fast but not so fast that consecutive analysis frames are far apart
    // in media time: at 8x a 4 fps analysis cadence still samples every ~2 s.
    m_player->setMuted(true);
    m_player->setSpeed(8.0);
    m_player->seek(0.0);
    m_player->play();
}

void VideoTile::finishScan()
{
    if (!m_scanning) {
        return;
    }
    m_scanning = false;
    if (m_player) {
        m_player->pause();
        m_player->setSpeed(m_scanPrevSpeed > 0.0 ? m_scanPrevSpeed : 1.0);
        m_player->setMuted(m_scanPrevMuted);
        // Leave the user where they were before the scan, not at the end.
        m_player->seek(m_scanPrevPos > 0.0 ? m_scanPrevPos : 0.0);
    }
    if (m_scanBtn) {
        m_scanBtn->setText(tr("扫描"));
        m_scanBtn->setChecked(false);
    }
    const QVector<TimelineSegment> segs = buildPersonTimeline(m_scanSamples, 3.0);
    if (qEnvironmentVariableIsSet("ZP_AI_TRACE")) {
        QStringList parts;
        for (const TimelineSegment &s : segs) {
            parts << QStringLiteral("%1-%2 x%3").arg(s.start, 0, 'f', 1)
                         .arg(s.end, 0, 'f', 1).arg(s.peak);
        }
        qInfo().noquote() << "[scan] samples=" << m_scanSamples.size()
                          << "segments=" << segs.size() << parts.join(QLatin1Char(' '));
    }
    if (m_posSlider) {
        m_posSlider->setSegments(segs);
    }
    if (m_timeLabel) {
        m_timeLabel->setText(formatTime(m_player ? m_player->position() : 0.0)
                             + QStringLiteral(" / ") + formatTime(m_duration));
    }
    emit scanFinished(m_channel, segs.size());
}

void VideoTile::cancelScan()
{
    if (!m_scanning) {
        return;
    }
    m_scanning = false;
    if (m_player) {
        m_player->pause();
        m_player->setSpeed(m_scanPrevSpeed > 0.0 ? m_scanPrevSpeed : 1.0);
        m_player->setMuted(m_scanPrevMuted);
        if (m_scanPrevPos > 0.0) {
            m_player->seek(m_scanPrevPos);
        }
    }
    if (m_scanBtn) {
        m_scanBtn->setText(tr("扫描"));
    }
    if (m_timeLabel) {
        m_timeLabel->setText(formatTime(m_player ? m_player->position() : 0.0)
                             + QStringLiteral(" / ") + formatTime(m_duration));
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
    if (m_scanning && result.pts >= 0.0) {
        int persons = 0;
        for (const AnalysisBox &b : result.boxes) {
            if (b.classId == 0 /* person */ && b.score >= 0.25f) {
                ++persons;
            }
        }
        PresenceSample s;
        s.t = result.pts;
        s.persons = persons;
        m_scanSamples.append(s);
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

void VideoTile::setOcrEnabled(bool on)
{
    if (m_ocrBtn) {
        m_ocrBtn->setChecked(on);   // emits ocrToggled
    }
}

void VideoTile::setRoiRect(const QRectF &roi)
{
    m_roi = roi.normalized();
    if (m_aiOverlay) {
        m_aiOverlay->setRoi(m_roi);
    }
    if (m_roiBtn) {
        m_roiBtn->setChecked(!m_roi.isEmpty());
    }
    if (m_zoomBtn) {
        m_zoomBtn->setEnabled(!m_roi.isEmpty());
    }
    if (m_roi.isEmpty() && m_zoomBtn && m_zoomBtn->isChecked()) {
        m_zoomBtn->setChecked(false);
    }
    emit roiChanged(m_channel, m_roi);
}

void VideoTile::setZoomEnabled(bool on)
{
    if (m_zoomBtn) {
        m_zoomBtn->setChecked(on);   // emits toggled -> setVideoCrop
    }
}

void VideoTile::setSharpenEnabled(bool on)
{
    if (m_sharpenBtn) {
        m_sharpenBtn->setChecked(on);
    }
}

bool VideoTile::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_video && event->type() == QEvent::Resize && m_aiOverlay && m_video) {
        m_aiOverlay->setGeometry(m_video->rect());
        m_aiOverlay->raise();
    }

    // ---- ROI rubber band ----
    if (watched == m_video && m_roiMode && m_video) {
        const QSizeF size = m_video->size();
        auto normalized = [&](const QPointF &p) {
            return QPointF(qBound(0.0, p.x() / qMax(1.0, size.width()), 1.0),
                           qBound(0.0, p.y() / qMax(1.0, size.height()), 1.0));
        };
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_draggingRoi = true;
                m_roiStart = normalized(me->pos());
                if (m_aiOverlay) {
                    m_aiOverlay->setRoiDraft(QRectF(m_roiStart, m_roiStart));
                }
                return true;   // swallow: the click is a selection, not a tile click
            }
        } else if (event->type() == QEvent::MouseMove && m_draggingRoi) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (m_aiOverlay) {
                m_aiOverlay->setRoiDraft(
                    QRectF(m_roiStart, normalized(me->pos())).normalized());
            }
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_draggingRoi) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_draggingRoi = false;
                const QRectF draft =
                    QRectF(m_roiStart, normalized(me->pos())).normalized();
                if (m_aiOverlay) {
                    m_aiOverlay->setRoiDraft(QRectF());
                }
                // A click without a drag (a few pixels) cancels the selection.
                if (draft.width() > 0.03 && draft.height() > 0.03) {
                    m_roi = draft;
                    m_roiMode = false;
                    if (m_video) {
                        m_video->unsetCursor();
                    }
                    if (m_aiOverlay) {
                        m_aiOverlay->setRoi(m_roi);
                    }
                    if (m_zoomBtn) {
                        m_zoomBtn->setEnabled(true);
                        if (m_zoomBtn->isChecked()) {
                            // Re-apply the zoom to the freshly framed region.
                            if (m_player) {
                                m_player->setVideoCrop(m_roi);
                            }
                        }
                    }
                    // While zoomed, analysis sees the whole (already cropped)
                    // picture — see the zoom toggle above.
                    emit roiChanged(m_channel, (m_zoomBtn && m_zoomBtn->isChecked())
                                                   ? QRectF() : m_roi);
                } else if (m_roiBtn) {
                    // Nothing framed: leave selection mode so the tile is
                    // normally clickable again. With an existing ROI the button
                    // stays checked (ROI still active).
                    m_roiMode = false;
                    if (m_video) {
                        m_video->unsetCursor();
                    }
                    m_roiBtn->setChecked(!m_roi.isEmpty());
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace zp
