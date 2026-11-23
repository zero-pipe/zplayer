#include "player/video_tile.h"
#include "train/dataset_store.h"
#include "player/media_factory.h"
#include "player/shaders.h"
#include "analysis/yolo_detector.h"
#include "ui/analysis_overlay.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/timeline_slider.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QPushButton>
#include <QSlider>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyle>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSignalBlocker>
#include <QTimer>
#include <QSettings>
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

    // --- Floating glass control bar over the video ---
    buildControlBar();

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(1, 1, 1, 1);
    vl->setSpacing(0);
    vl->addWidget(header);
    vl->addWidget(stageWidget, 1);

    connect(m_player, &IMediaPlayer::started, this, [this]() {
        m_hasPlayed = true;
        maybeShowHint();
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

    connect(m_player, &IMediaPlayer::stateChanged, this, [this](MediaState s) {
        updateTransportUi();
        emit playbackStateChanged(m_channel, s == MediaState::Playing);
        if (s == MediaState::Paused && m_video) {
            // Paused keeps the current frame on screen (mpv does not clear it).
            m_stage->setCurrentWidget(m_video);
        }
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
    // Local files are on-demand; platform streams are live previews and get
    // no transport controls.
    m_isVod = url.isLocalFile();
    setState(Connecting, tr("连接中…"));
    positionControlBar();
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

void VideoTile::captureSample(const QString &reason)
{
    if (!m_player || m_lastBoxes.isEmpty()) {
        emit sampleCaptured(m_channel, false);
        return;
    }
    DatasetStore &ds = DatasetStore::instance();
    // Detections carry the model's own class index, which is only a COCO id for
    // the bundled model. Project samples must reference the project class table,
    // so resolve each box by label (and auto-register unseen classes, e.g. 安全帽
    // coming from a freshly retrained model).
    QVector<AnalysisBox> boxes = m_lastBoxes;
    const QVector<ClassDef> cls = ds.classes();
    for (AnalysisBox &b : boxes) {
        bool matched = false;
        for (const ClassDef &c : cls) {
            if ((!b.label.isEmpty() && b.label.compare(c.name, Qt::CaseInsensitive) == 0)
                || (!b.label.isEmpty() && !c.zh.isEmpty() && b.label == c.zh)) {
                b.classId = c.id;
                b.label = c.zh.isEmpty() ? c.name : c.zh;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        if (ds.classIndex(b.classId) >= 0) {
            b.label = ds.className(b.classId);
            continue;
        }
        if (!b.label.isEmpty()) {
            bool nonAscii = false;
            for (const QChar ch : b.label) {
                if (ch.unicode() > 0x7F) {
                    nonAscii = true;
                    break;
                }
            }
            const int id = ds.addClass(nonAscii ? QString() : b.label,
                                       nonAscii ? b.label : QString(), nullptr);
            if (id >= 0) {
                b.classId = id;
            }
        }
    }
    const QString name = m_name.isEmpty() ? m_channel.channelId : m_name;
    const QString path = ds.newFramePath(name);
    const bool ok = m_player->grabSnapshot(path);
    if (ok) {
        ds.add(path, name, m_capturePts, reason, boxes);
    }
    emit sampleCaptured(m_channel, ok);
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
    // before the first frame can be decoded). Once a stream has played, Idle
    // keeps the last frame on screen (mpv keep-open) so the control bar stays
    // usable instead of being replaced by the status panel.
    const bool hasPicture = m_hasPlayed && m_video;
    if (state == Playing || state == Connecting || (state == Idle && hasPicture)) {
        m_stage->setCurrentWidget(m_video ? m_video : m_overlay);
    } else {
        m_stage->setCurrentWidget(m_overlay);
    }
    updateTransportUi();
    updateCaptureUi();
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

// ---------------------------------------------------------------------------
// Floating glass control bar
//
// The bar floats over the bottom of the video (like modern VMS clients /
// streaming players) instead of occupying a strip under the picture, and is
// grouped left→right: transport · timeline · AI strategy · picture · evidence.
// ---------------------------------------------------------------------------
void VideoTile::buildControlBar()
{
    if (!m_video) {
        return;
    }
    m_footer = new QWidget(m_video);
    m_footer->setObjectName(QStringLiteral("TileBar"));
    m_footer->setProperty("dock", QStringLiteral("bottom"));
    m_footer->setAttribute(Qt::WA_StyledBackground, true);
    m_footer->installEventFilter(this);
    auto *row = new QHBoxLayout(m_footer);
    row->setContentsMargins(10, 5, 10, 5);
    row->setSpacing(4);

    auto makeBtn = [this](const QString &text) {
        auto *b = new QToolButton(m_footer);
        b->setObjectName(QStringLiteral("BarBtn"));
        b->setText(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    // The floating bar is always a dark glass strip (independent of the light/
    // dark theme), so its glyphs use a fixed light palette: muted grey at rest,
    // a soft brand blue when active. This keeps the bar readable and calm in
    // both themes instead of dark theme-tokens sinking into the glass.
    auto barIcon = [](icons::Glyph g) {
        const QColor rest(0xC7, 0xCD, 0xD4);
        const QColor active(0x8A, 0xB4, 0xF8);
        QIcon ic;
        ic.addPixmap(icons::glyph(g, rest, 16), QIcon::Normal, QIcon::Off);
        ic.addPixmap(icons::glyph(g, active, 16), QIcon::Normal, QIcon::On);
        ic.addPixmap(icons::glyph(g, active, 16), QIcon::Active, QIcon::Off);
        return ic;
    };
    auto dressBtn = [barIcon](QToolButton *b, icons::Glyph g) {
        b->setIcon(barIcon(g));
        b->setIconSize(QSize(16, 16));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    };
    auto addSep = [this, row]() {
        auto *line = new QFrame(m_footer);
        line->setObjectName(QStringLiteral("BarSep"));
        line->setFrameShape(QFrame::VLine);
        line->setFixedHeight(18);
        row->addWidget(line);
    };

    m_hasTransport = m_player && m_player->supportsPlaybackControl();

    // ---- transport row (own row, above the action bar, VOD only) ----
    m_transportBar = new QWidget(m_video);
    m_transportBar->setObjectName(QStringLiteral("TileBar"));
    m_transportBar->setAttribute(Qt::WA_StyledBackground, true);
    auto *trow = new QHBoxLayout(m_transportBar);
    trow->setContentsMargins(10, 4, 10, 4);
    trow->setSpacing(6);

    if (m_hasTransport) {
        m_playBtn = new QToolButton(m_transportBar);
        m_playBtn->setObjectName(QStringLiteral("BarPlay"));
        m_playBtn->setCursor(Qt::PointingHandCursor);
        m_playBtn->setFocusPolicy(Qt::NoFocus);
        m_playBtn->setIcon(QIcon(icons::playPause(false, QColor(0xD5DEEA), 15)));
        m_playBtn->setIconSize(QSize(15, 15));
        m_playBtn->setToolTip(tr("播放 / 暂停"));
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

        m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), m_transportBar);
        m_timeLabel->setObjectName(QStringLiteral("BarTime"));

        m_posSlider = new TimelineSlider(Qt::Horizontal, m_transportBar);
        m_posSlider->setObjectName(QStringLiteral("BarSeek"));
        m_posSlider->setRange(0, 0);
        m_posSlider->setMinimumWidth(90);
        m_posSlider->setCursor(Qt::PointingHandCursor);
        m_posSlider->setToolTip(tr("拖动定位；点击时间轴色块可跳转"));
        connect(m_posSlider, &TimelineSlider::segmentActivated, this, [this](double t) {
            if (!m_player) {
                return;
            }
            // Jumping to a segment should start playing there, not wait for a
            // separate play click.
            m_player->seek(t);
            if (m_player->state() != MediaState::Playing) {
                m_player->play();
            }
        });
        connect(m_posSlider, &QSlider::sliderPressed, this, [this]() { m_scrubbing = true; });
        connect(m_posSlider, &QSlider::sliderReleased, this, [this]() {
            m_scrubbing = false;
            if (m_player && m_duration > 0) {
                // Release-to-seek-and-play: scrub to a new spot and playback
                // resumes there immediately (mainstream player behaviour),
                // even if it was paused beforehand.
                m_player->seek(m_posSlider->value() / 1000.0);
                if (m_player->state() != MediaState::Playing) {
                    m_player->play();
                }
            }
        });
        connect(m_posSlider, &QSlider::valueChanged, this, [this](int v) {
            if (m_scrubbing && m_timeLabel) {
                m_timeLabel->setText(formatTime(v / 1000.0)
                                     + QStringLiteral(" / ") + formatTime(m_duration));
            }
        });

        // Speed: dropdown with the standard ladder.
        m_speedBtn = new QToolButton(m_transportBar);
        m_speedBtn->setObjectName(QStringLiteral("BarBtn"));
        m_speedBtn->setText(QStringLiteral("1.0x"));
        m_speedBtn->setCursor(Qt::PointingHandCursor);
        m_speedBtn->setFocusPolicy(Qt::NoFocus);
        m_speedBtn->setToolTip(tr("播放速度"));
        m_speedMenu = new QMenu(m_speedBtn);
        for (double s : {1.0, 2.0, 4.0, 8.0, 16.0}) {
            QAction *a = m_speedMenu->addAction(QStringLiteral("%1x").arg(s, 0, 'g', 2));
            a->setCheckable(true);
            a->setChecked(qFuzzyCompare(s, 1.0));
            connect(a, &QAction::triggered, this, [this, s]() { setSpeed(s); });
        }
        m_speedBtn->setMenu(m_speedMenu);
        m_speedBtn->setPopupMode(QToolButton::InstantPopup);

        trow->addWidget(m_playBtn);
        trow->addWidget(m_timeLabel);
        trow->addWidget(m_posSlider, 1);
        trow->addWidget(m_speedBtn);
    }

    // ---- right group 1: AI check (one switch + settings menu) ----
    // The old "AI skip" toggle and its separate caret are merged here: the
    // button itself is the master switch, the menu decides what counts as a
    // target and which actions fire (fast-forward when empty, pause on hit).
    m_aiBtn = makeBtn(tr("AI 检查"));
    dressBtn(m_aiBtn, icons::AiScan);
    m_aiBtn->setCheckable(true);
    m_aiBtn->setToolTip(tr("开启 AI 检查（框选目标、无目标快进、检出暂停）"));
    connect(m_aiBtn, &QToolButton::toggled, this, [this](bool on) {
        setAiSkipEnabled(on);
        emit statusMessage(m_channel, on ? tr("AI 检查：开") : tr("AI 检查：关"));
    });
    // Persisted similarity threshold (percent). Default 80% — low-confidence
    // boxes around 30% were too noisy to read.
    m_aiMinScore = qBound(0.30f, QSettings().value(QStringLiteral("ai/minScorePct"), 80).toInt() / 100.0f, 0.95f);
    applyDefaultAiClasses();
    buildAiMenu();

    // 采集: save the current frame (+ last detections) as a training sample.
    // Only meaningful with AI check on and the global capture switch on.
    m_captureBtn = makeBtn(tr("采集"));
    dressBtn(m_captureBtn, icons::Layers);
    m_captureBtn->setToolTip(tr("采集当前画面为训练样本（需开启 AI 检查与样本采集）"));
    connect(m_captureBtn, &QToolButton::clicked, this, [this]() {
        if (!m_aiSkip) {
            emit statusMessage(m_channel, tr("请先开启 AI 检查"));
            return;
        }
        if (!m_captureEnabled) {
            emit statusMessage(m_channel, tr("请在 AI 检查菜单中开启「样本采集」"));
            return;
        }
        captureSample(QStringLiteral("manual"));
        updateCaptureUi();
    });
    updateCaptureUi();

    m_ocrBtn = makeBtn(tr("OCR识别"));
    dressBtn(m_ocrBtn, icons::Ocr);
    m_ocrBtn->setCheckable(true);
    m_ocrBtn->setToolTip(tr("识别画面中的文字/车牌；配合 ROI 可只识别局部"));
    connect(m_ocrBtn, &QToolButton::toggled, this, [this](bool on) {
        emit ocrToggled(m_channel, on);
        emit statusMessage(m_channel, on ? tr("OCR 识别：开") : tr("OCR 识别：关"));
    });

    m_roiBtn = makeBtn(tr("ROI框"));
    dressBtn(m_roiBtn, icons::Roi);
    m_roiBtn->setCheckable(true);
    m_roiBtn->setToolTip(tr("圈选重点分析区域：点击后拖动框选，再点一次清除"));
    connect(m_roiBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (!checked) {
            m_roiMode = false;
            m_roi = QRectF();
            if (m_zoomLevel > 0) {
                setZoomLevel(0);   // zoom base was the ROI; restore the picture
            }
            if (m_aiOverlay) {
                m_aiOverlay->setRoi(QRectF());
                m_aiOverlay->setRoiDraft(QRectF());
            }
            if (m_video) {
                m_video->unsetCursor();
            }
            emit roiChanged(m_channel, QRectF());
            emit statusMessage(m_channel, tr("ROI 已清除"));
            return;
        }
        m_roiMode = true;
        if (m_video) {
            m_video->setCursor(Qt::CrossCursor);
        }
        emit statusMessage(m_channel, tr("在画面上拖动框选分析区域"));
    });

    // ---- right group 2: picture ----
    m_zoomBtn = makeBtn(tr("电子放大"));
    dressBtn(m_zoomBtn, icons::Magnify);
    m_zoomBtn->setToolTip(tr("点击循环 1x/2x/4x；放大后可滚轮切换倍数、拖动平移画面"));
    m_zoomBtn->setEnabled(m_player && m_player->supportsVideoCrop());
    connect(m_zoomBtn, &QToolButton::clicked, this, [this]() {
        setZoomLevel((m_zoomLevel + 1) % 3);
    });

    m_sharpenBtn = makeBtn(tr("画质锐化"));
    dressBtn(m_sharpenBtn, icons::Sparkle);
    m_sharpenBtn->setCheckable(true);
    m_sharpenBtn->setToolTip(tr("GPU 锐化增强（配合电子放大观看细节）"));
    connect(m_sharpenBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!m_player) {
            return;
        }
        m_player->setShader(on ? ensureSharpenShaderFile() : QString());
        emit statusMessage(m_channel, on ? tr("画质锐化：开") : tr("画质锐化：关"));
    });

    // Voice denoising: rnnoise through the engine's audio filter chain.
    m_denoiseBtn = makeBtn(tr("语音降噪"));
    dressBtn(m_denoiseBtn, icons::Wave);
    m_denoiseBtn->setCheckable(true);
    m_denoiseBtn->setToolTip(tr("rnnoise 实时降噪：抑制风噪/机械底噪，突出人声"));
    m_denoiseBtn->setEnabled(m_player && m_player->supportsDenoise());
    connect(m_denoiseBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!m_player) {
            return;
        }
        if (on && !m_player->setDenoise(true)) {
            // Missing model / filter: revert without re-entering this handler.
            const QSignalBlocker blocker(m_denoiseBtn);
            m_denoiseBtn->setChecked(false);
            emit statusMessage(m_channel, tr("语音降噪不可用（缺少 rnnoise 模型）"));
            return;
        }
        if (!on) {
            m_player->setDenoise(false);
        }
        emit statusMessage(m_channel, on ? tr("语音降噪：开") : tr("语音降噪：关"));
    });

    // ---- right group 3: search & evidence ----
    m_scanBtn = makeBtn(tr("智能检索"));
    dressBtn(m_scanBtn, icons::SearchClock);
    m_scanBtn->setCheckable(true);
    m_scanBtn->setToolTip(tr("快速扫描全片，在时间轴标出有目标的片段"));
    connect(m_scanBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (checked) {
            startScan();
        } else {
            cancelScan();
        }
    });

    m_snapBtn = makeBtn(tr("抓图"));
    dressBtn(m_snapBtn, icons::Snapshot);
    m_snapBtn->setToolTip(tr("抓图"));
    connect(m_snapBtn, &QToolButton::clicked, this, &VideoTile::takeSnapshot);

    m_evidenceBtn = makeBtn(tr("导出证据"));
    dressBtn(m_evidenceBtn, icons::Export);
    m_evidenceBtn->setToolTip(tr("导出事件列表与截图（HTML + JSON）"));
    connect(m_evidenceBtn, &QToolButton::clicked, this, [this]() {
        emit evidenceExportRequested(m_channel);
    });

    // ---- assemble: action groups, right-aligned (transport has its own row
    // above this bar) ----
    row->addStretch(1);
    row->addWidget(m_aiBtn);
    row->addWidget(m_captureBtn);
    row->addWidget(m_ocrBtn);
    row->addWidget(m_roiBtn);
    addSep();
    row->addWidget(m_zoomBtn);
    row->addWidget(m_sharpenBtn);
    row->addWidget(m_denoiseBtn);
    addSep();
    row->addWidget(m_scanBtn);
    row->addWidget(m_snapBtn);
    row->addWidget(m_evidenceBtn);

    if (m_hasTransport) {
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
            if (m_speedMenu) {
                for (QAction *a : m_speedMenu->actions()) {
                    a->setChecked(false);
                }
            }
        });
    }

    positionControlBar();
    updateEvidenceBadge();
}

void VideoTile::maybeShowHint()
{
    if (m_hint) {
        return;
    }
    QSettings settings;
    if (settings.value(QStringLiteral("ui/aiHintShown"), false).toBool()) {
        return;
    }
    settings.setValue(QStringLiteral("ui/aiHintShown"), true);
    m_hint = new QLabel(m_video);
    m_hint->setObjectName(QStringLiteral("VideoHint"));
    m_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hint->setText(tr("提示：底栏可开启 AI 检查 / OCR 识别 / ROI 圈选，悬停按钮可查看说明"));
    m_hint->adjustSize();
    m_hint->show();
    positionControlBar();
    QTimer::singleShot(6500, this, [this]() {
        if (m_hint) {
            m_hint->deleteLater();
            m_hint = nullptr;
        }
    });
}

void VideoTile::positionControlBar()
{
    if (!m_video || !m_footer) {
        return;
    }
    const QRect r = m_video->rect();
    // Small tiles (4x4 layouts) keep the bars hidden until the pointer is over
    // the tile, so a 16-way grid stays clean but every tile remains reachable.
    const bool small = r.width() < 380 || r.height() < 150;
    if (small && !m_hovered) {
        m_footer->hide();
        if (m_transportBar) {
            m_transportBar->hide();
        }
        return;
    }

    const int margin = 8;
    // Action bar is docked: full width, flush to the bottom of the video, so a
    // solid strip covers the letterbox instead of a translucent pill floating
    // over it. The transport row above stays a floating pill.
    const int fh = qMax(30, m_footer->sizeHint().height());
    const int fy = r.height() - fh;
    m_footer->setGeometry(0, fy, r.width(), fh);
    m_footer->show();
    m_footer->raise();

    // Transport lives on its own row above the action bar, and only exists for
    // on-demand playback: live preview has no seekable timeline.
    const bool showTransport = m_transportBar && m_hasTransport && m_isVod;

    if (m_hint) {
        m_hint->adjustSize();
        const int top = (showTransport && m_transportBar->isVisible())
                            ? m_transportBar->geometry().top() : fy;
        m_hint->move(qMax(margin, (r.width() - m_hint->width()) / 2),
                     qMax(margin, top - m_hint->height() - 8));
        m_hint->raise();
    }
    if (showTransport) {
        const int th = qMax(30, m_transportBar->sizeHint().height());
        const int tw = qMax(120, r.width() - 2 * margin);
        m_transportBar->setGeometry(margin, fy - th - 6, tw, th);
        m_transportBar->show();
        m_transportBar->raise();
    } else if (m_transportBar) {
        m_transportBar->hide();
    }
}

void VideoTile::updateEvidenceBadge()
{
    if (!m_evidenceBtn) {
        return;
    }
    const int n = m_evidence.size();
    m_evidenceBtn->setText(n > 0 ? tr("导出证据 ·%1").arg(n) : tr("导出证据"));
}

void VideoTile::setSpeed(double speed)
{
    if (!m_player) {
        return;
    }
    m_player->setSpeed(speed);
}

// ---------------------------------------------------------------------------
// Electronic zoom (1x / 2x / 4x, wheel to change, drag to pan)
// ---------------------------------------------------------------------------
QRectF VideoTile::zoomCropRect() const
{
    const QRectF base = m_roi.isEmpty() ? QRectF(0.0, 0.0, 1.0, 1.0) : m_roi;
    const double scale = 1.0 / double(1 << m_zoomLevel);   // level 1 -> 1/2, 2 -> 1/4
    const double w = base.width() * scale;
    const double h = base.height() * scale;
    QPointF c(m_zoomCenter);
    if (w < base.width()) {
        c.setX(qBound(base.left() + w / 2.0, c.x(), base.right() - w / 2.0));
    }
    if (h < base.height()) {
        c.setY(qBound(base.top() + h / 2.0, c.y(), base.bottom() - h / 2.0));
    }
    return QRectF(c.x() - w / 2.0, c.y() - h / 2.0, w, h);
}

void VideoTile::applyZoom()
{
    if (!m_player) {
        return;
    }
    if (m_zoomLevel <= 0) {
        m_player->setVideoCrop(QRectF());
        if (m_aiOverlay) {
            m_aiOverlay->setRoi(m_roi);
        }
        // Analysis ROI applies again once the full picture is visible.
        emit roiChanged(m_channel, m_roi);
        if (m_video) {
            m_video->unsetCursor();
        }
        return;
    }
    m_player->setVideoCrop(zoomCropRect());
    // The visible picture IS the crop, so the service must not re-apply the
    // ROI, and the dashed frame would be meaningless over the zoomed view.
    if (m_aiOverlay) {
        m_aiOverlay->setRoi(QRectF());
    }
    emit roiChanged(m_channel, QRectF());
    if (m_video && !m_roiMode) {
        m_video->setCursor(Qt::OpenHandCursor);
    }
}

void VideoTile::setZoomLevel(int level)
{
    m_zoomLevel = qBound(0, level, 2);
    if (m_zoomLevel > 0) {
        const QRectF base = m_roi.isEmpty() ? QRectF(0.0, 0.0, 1.0, 1.0) : m_roi;
        m_zoomCenter = base.center();
    }
    if (m_zoomBtn) {
        m_zoomBtn->setText(m_zoomLevel > 0
                               ? tr("电子放大 %1x").arg(1 << m_zoomLevel)
                               : tr("电子放大"));
    }
    applyZoom();
    emit statusMessage(m_channel,
                       m_zoomLevel > 0
                           ? tr("电子放大：%1x（滚轮调整，拖动平移）").arg(1 << m_zoomLevel)
                           : tr("电子放大：关"));
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
        m_scanBtn->setText(tr("智能检索"));
        m_scanBtn->setChecked(false);
    }
    const QVector<TimelineSegment> segs = buildTimeline(m_scanSamples, 3.0);
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
        m_scanBtn->setText(tr("智能检索"));
    }
    if (m_timeLabel) {
        m_timeLabel->setText(formatTime(m_player ? m_player->position() : 0.0)
                             + QStringLiteral(" / ") + formatTime(m_duration));
    }
}

void VideoTile::updateTransportUi()
{
    if (m_playBtn) {
        // The engine is the source of truth: after pausing it reports Paused,
        // which is what flips the icon (the tile's own state only tracks the
        // higher-level display states).
        const bool playing = m_player && m_player->state() == MediaState::Playing;
        m_playBtn->setIcon(QIcon(icons::playPause(
            playing, playing ? QColor(0x2F, 0x81, 0xF7) : QColor(0xD5, 0xDE, 0xEA), 15)));
        m_playBtn->setToolTip(playing ? tr("暂停") : tr("播放"));
    }
}

// ---------------------------------------------------------------------------
// AI overlay
// ---------------------------------------------------------------------------
void VideoTile::setAnalysisResult(const zp::AnalysisResult &result)
{
    // Remember the latest result so a failure sample can be captured on demand.
    m_lastBoxes = result.boxes;
    m_capturePts = result.pts;

    // Detection boxes are only drawn while AI check is enabled; switching it
    // off clears the overlay. The analysis pipeline keeps running so the
    // on-demand 智能检索 (scan) and evidence export still work.
    if (m_aiOverlay) {
        if (m_aiSkip) {
            QVector<AnalysisBox> shown;
            shown.reserve(result.boxes.size());
            for (const AnalysisBox &b : result.boxes) {
                if (b.score >= m_aiMinScore) {
                    shown.append(b);
                }
            }
            m_aiOverlay->setBoxes(shown, result.videoRect);
        } else {
            m_aiOverlay->clear();
        }
    }
    if (m_scanning && result.pts >= 0.0) {
        PresenceSample s;
        s.t = result.pts;
        const bool coco = cocoModel();
        for (const AnalysisBox &b : result.boxes) {
            if (b.score < m_aiMinScore) {
                continue;
            }
            if (!coco) {
                // Custom model: any configured class counts as presence.
                if (m_aiClasses.contains(b.classId)) {
                    s.persons++;
                }
                continue;
            }
            switch (b.classId) {
            case 0: s.persons++; break;
            case 2: case 3: case 5: case 7: s.vehicles++; break;
            case 15: case 16: case 17: case 18: case 19:
            case 20: case 21: case 22: case 23: s.animals++; break;
            default: break;
            }
        }
        m_scanSamples.append(s);
    }
    updateAiSkip(result.pts, result.boxes);
    updateEvidence(result.pts, result.boxes);

    // Auto-collect "hard" samples: a plausible but low-confidence box (below
    // the display threshold) is exactly what retraining benefits from most.
    // Only runs while the user has switched 样本采集 on, and is throttled per
    // tile so a busy scene cannot flood the dataset.
    if (m_captureEnabled && !m_scanning && m_state == Playing && m_player
        && m_player->supportsFrameTap()) {
        bool suspicious = false;
        for (const AnalysisBox &b : result.boxes) {
            if (b.score >= 0.15f && b.score < m_aiMinScore) {
                suspicious = true;
                break;
            }
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (suspicious && now - m_lastCaptureMs > 6000) {
            m_lastCaptureMs = now;
            captureSample(QStringLiteral("low_conf"));
        }
    }
}

void VideoTile::setAiSkipEnabled(bool on)
{
    m_aiSkip = on;
    m_lastPresenceMs = 0;
    m_lastSeekMs = 0;
    if (!on && m_aiOverlay) {
        m_aiOverlay->clear();   // hide boxes immediately when AI check is off
    }
    if (m_aiBtn && m_aiBtn->isChecked() != on) {
        m_aiBtn->setChecked(on);
    }
    updateCaptureUi();
}

// ---------------------------------------------------------------------------
// Sample collection (capture switch + manual 采集 button)
// ---------------------------------------------------------------------------
bool VideoTile::cocoModel() const
{
    // An empty list means "no detector metadata" (e.g. the dev stats analyzer);
    // keep the COCO semantics then so existing behaviour is unchanged.
    return m_classNames.isEmpty() || YoloDetector::looksLikeCoco(m_classNames);
}

void VideoTile::setClassNames(const QStringList &names)
{
    if (m_classNames == names) {
        return;
    }
    m_classNames = names;
    applyDefaultAiClasses();
    buildAiMenu();   // target semantics / labels depend on the model's classes
}

void VideoTile::setCaptureEnabled(bool on)
{
    m_captureEnabled = on;
    if (m_captureAct && m_captureAct->isChecked() != on) {
        const QSignalBlocker blocker(m_captureAct);
        m_captureAct->setChecked(on);
    }
    updateCaptureUi();
}

void VideoTile::applyDefaultAiClasses()
{
    m_aiClasses.clear();
    if (cocoModel()) {
        for (int id : {0, 2, 3, 5, 7, 16}) {
            m_aiClasses.insert(id);
        }
    } else {
        for (int i = 0; i < m_classNames.size(); ++i) {
            m_aiClasses.insert(i);
        }
    }
}

void VideoTile::updateCaptureUi()
{
    if (!m_captureBtn) {
        return;
    }
    const bool ready = m_aiSkip && m_captureEnabled && m_state == Playing
                       && m_player != nullptr;
    m_captureBtn->setEnabled(ready);
}

void VideoTile::buildAiMenu()
{
    if (!m_aiBtn) {
        return;
    }
    if (m_aiMenu) {
        m_aiBtn->setMenu(nullptr);
        m_aiMenu->deleteLater();
        m_aiMenu = nullptr;
    }
    auto *aiMenu = new QMenu(m_aiBtn);
    m_aiMenu = aiMenu;
    const bool coco = cocoModel();

    if (coco) {
        aiMenu->addAction(tr("检查目标"))->setEnabled(false);
        auto addTarget = [this, aiMenu](const QString &label, const QVector<int> &ids,
                                        bool on) {
            QAction *a = aiMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(on);
            connect(a, &QAction::toggled, this, [this, ids](bool on2) {
                for (int id : ids) {
                    if (on2) {
                        m_aiClasses.insert(id);
                    } else {
                        m_aiClasses.remove(id);
                    }
                }
                emit statusMessage(m_channel, tr("AI 检查目标已更新"));
            });
        };
        addTarget(tr("人"), {0}, m_aiClasses.contains(0));
        addTarget(tr("车"), {2, 3, 5, 7}, m_aiClasses.contains(2));
        addTarget(tr("狗"), {16}, m_aiClasses.contains(16));
    } else {
        // Custom / retrained model: its class ids have no COCO meaning, so
        // "人/车/狗" would be wrong. Every class is a target instead.
        QAction *note = aiMenu->addAction(tr("自定义类别模型：全部类别参与检查"));
        note->setEnabled(false);
    }

    QMenu *scoreMenu = aiMenu->addMenu(tr("相似度阈值"));
    auto *scoreGroup = new QActionGroup(scoreMenu);
    scoreGroup->setExclusive(true);
    const int curPct = qRound(m_aiMinScore * 100.0f);
    for (int pct : {30, 40, 50, 60, 70, 80}) {
        QAction *a = scoreMenu->addAction(QStringLiteral("%1%").arg(pct));
        a->setCheckable(true);
        a->setActionGroup(scoreGroup);
        a->setChecked(pct == curPct);
        connect(a, &QAction::triggered, this, [this, pct]() {
            m_aiMinScore = pct / 100.0f;
            QSettings().setValue(QStringLiteral("ai/minScorePct"), pct);
            emit statusMessage(m_channel, tr("相似度阈值：%1%").arg(pct));
        });
    }

    aiMenu->addSeparator();
    const QString skipText = coco ? tr("无人时快进跳过") : tr("无目标时快进跳过");
    QAction *skipAct = aiMenu->addAction(skipText);
    skipAct->setCheckable(true);
    skipAct->setChecked(m_aiSkipEmpty);
    connect(skipAct, &QAction::toggled, this, [this, skipText](bool on) {
        m_aiSkipEmpty = on;
        emit statusMessage(m_channel,
                           on ? tr("%1：开").arg(skipText) : tr("%1：关").arg(skipText));
    });
    QAction *pauseAct = aiMenu->addAction(tr("检出目标自动暂停"));
    pauseAct->setCheckable(true);
    pauseAct->setChecked(m_aiPauseOnHit);
    connect(pauseAct, &QAction::toggled, this, [this](bool on) {
        m_aiPauseOnHit = on;
        emit statusMessage(m_channel, on ? tr("检出自动暂停：开") : tr("检出自动暂停：关"));
    });

    // Sample collection switch: automatic low-confidence frames + the 采集
    // button. Off by default; the choice is global (window persists it).
    aiMenu->addSeparator();
    m_captureAct = aiMenu->addAction(tr("样本采集（保存失败帧）"));
    m_captureAct->setCheckable(true);
    m_captureAct->setChecked(m_captureEnabled);
    m_captureAct->setToolTip(tr("开启后自动收集低置信帧，并允许用「采集」按钮手动保存当前画面"));
    connect(m_captureAct, &QAction::toggled, this, [this](bool on) {
        setCaptureEnabled(on);
        emit captureSettingChanged(on);
        emit statusMessage(m_channel, on ? tr("样本采集：开") : tr("样本采集：关"));
    });

    m_aiBtn->setMenu(aiMenu);
    // Main area toggles the switch, the caret opens the settings.
    m_aiBtn->setPopupMode(QToolButton::MenuButtonPopup);
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

    bool present = false;
    for (const AnalysisBox &b : boxes) {
        if (b.score >= m_aiMinScore && m_aiClasses.contains(b.classId)) {
            present = true;
            break;
        }
    }
    if (present) {
        m_lastPresenceMs = now;
        if (m_aiSkipping) {
            // A configured target appeared after a skip: settle back to normal
            // speed so the operator can actually watch what showed up.
            m_aiSkipping = false;
            if (m_player->speed() > 1.001) {
                m_player->setSpeed(1.0);
            }
        }
        if (m_aiPauseOnHit && !m_aiPausedForHit && !m_scanning) {
            m_aiPausedForHit = true;
            m_player->pause();
            emit statusMessage(m_channel, tr("AI 检查：检出目标，已暂停"));
        }
        return;
    }
    m_aiPausedForHit = false;
    if (!m_aiSkipEmpty) {
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
    m_aiSkipping = true;
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
        if (b.score < m_aiMinScore || !isSalientClass(b.classId)) {
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
    updateEvidenceBadge();
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
    if (m_roi.isEmpty() && m_zoomLevel > 0) {
        setZoomLevel(0);
    } else if (m_zoomLevel > 0) {
        m_zoomCenter = m_roi.center();
        applyZoom();
    }
    emit roiChanged(m_channel, m_roi);
}

void VideoTile::setZoomEnabled(bool on)
{
    setZoomLevel(on ? 1 : 0);
}

void VideoTile::setSharpenEnabled(bool on)
{
    if (m_sharpenBtn) {
        m_sharpenBtn->setChecked(on);
    }
}

void VideoTile::setDenoiseEnabled(bool on)
{
    if (m_denoiseBtn) {
        m_denoiseBtn->setChecked(on);
    }
}

bool VideoTile::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_video && event->type() == QEvent::Resize && m_aiOverlay && m_video) {
        m_aiOverlay->setGeometry(m_video->rect());
        m_aiOverlay->raise();
        positionControlBar();
    }

    // ---- hover reveal for small tiles ----
    if (watched == m_video && m_video && event->type() == QEvent::Enter) {
        m_hovered = true;
        positionControlBar();
    } else if (watched == m_video && m_video && event->type() == QEvent::Leave) {
        // Moving onto the bar itself (a child) must not hide it mid-click.
        if (!m_footer || !m_footer->underMouse()) {
            m_hovered = false;
            positionControlBar();
        }
    } else if (watched == m_footer && m_footer && event->type() == QEvent::Leave) {
        if (!m_video || !m_video->underMouse()) {
            m_hovered = false;
            positionControlBar();
        }
    }

    // ---- ROI rubber band ----
    // ---- wheel zoom + drag pan while the electronic zoom is active ----
    if (watched == m_video && m_video && !m_roiMode && m_zoomLevel > 0) {
        if (event->type() == QEvent::Wheel) {
            auto *we = static_cast<QWheelEvent *>(event);
            const int dy = we->angleDelta().y();
            if (dy != 0) {
                setZoomLevel(dy > 0 ? qMin(2, m_zoomLevel + 1)
                                    : qMax(0, m_zoomLevel - 1));
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_panning = true;
                m_panLast = me->pos();
                m_video->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_panning) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QSizeF size = m_video->size();
            if (size.width() > 0 && size.height() > 0) {
                // Move the crop opposite to the drag so the picture follows the
                // cursor, scaled by how much of the frame the crop covers.
                const QRectF crop = zoomCropRect();
                const double dx = -(me->pos().x() - m_panLast.x()) / size.width()
                                  * crop.width();
                const double dy = -(me->pos().y() - m_panLast.y()) / size.height()
                                  * crop.height();
                m_zoomCenter += QPointF(dx, dy);
                m_panLast = me->pos();
                applyZoom();
            }
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_panning) {
            m_panning = false;
            m_video->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }

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
                    if (m_zoomLevel > 0) {
                        // Re-apply the zoom with the freshly framed ROI as base.
                        m_zoomCenter = m_roi.center();
                        applyZoom();
                    } else {
                        emit roiChanged(m_channel, m_roi);
                    }
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
