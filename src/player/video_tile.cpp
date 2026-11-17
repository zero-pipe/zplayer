#include "player/video_tile.h"

#include <QtAV/AVPlayer.h>
#include <QtAV/VideoOutput.h>
#include <QtAV/AVError.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>

namespace zp {

VideoTile::VideoTile(const QString &deviceId, const QString &name, bool ptzCapable, QWidget *parent)
    : QWidget(parent)
    , m_deviceId(deviceId)
    , m_name(name)
    , m_ptzCapable(ptzCapable)
{
    setObjectName(QStringLiteral("VideoTile"));

    m_frame = new QWidget(this);
    m_frame->setObjectName(QStringLiteral("tileFrame"));

    auto *title = new QLabel(name, this);
    title->setObjectName(QStringLiteral("tileTitle"));
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_status = new QLabel(tr("未连接"), this);
    m_status->setObjectName(QStringLiteral("tileStatus"));

    auto *closeBtn = new QPushButton(QStringLiteral("×"), this);
    closeBtn->setObjectName(QStringLiteral("tileClose"));
    closeBtn->setFixedSize(22, 22);
    closeBtn->setCursor(Qt::ArrowCursor);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested(m_deviceId);
    });

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("tileHeader"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(6, 2, 4, 2);
    hl->setSpacing(6);
    hl->addWidget(title);
    hl->addWidget(m_status);
    hl->addWidget(closeBtn);

    m_player = new QtAV::AVPlayer(this);
    m_output = new QtAV::VideoOutput(this);
    m_video = m_output->widget();
    if (m_video) {
        m_video->setObjectName(QStringLiteral("tileVideo"));
        m_video->setMinimumSize(80, 45);
        m_video->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_player->setRenderer(m_output);
    }

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(1, 1, 1, 1);
    vl->setSpacing(0);
    vl->addWidget(header);
    if (m_video) {
        vl->addWidget(m_video, 1);
    }

    connect(m_player, &QtAV::AVPlayer::started, this, [this]() {
        setStatus(tr("播放中"), QStringLiteral("#4caf50"));
    });
    connect(m_player, &QtAV::AVPlayer::stopped, this, [this]() {
        setStatus(tr("已停止"), QStringLiteral("#9e9e9e"));
    });
    connect(m_player, &QtAV::AVPlayer::error, this, [this](const QtAV::AVError &e) {
        setStatus(tr("失败"), QStringLiteral("#f44336"));
        m_status->setToolTip(e.string());
    });

    setSelected(false);
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
        setStatus(tr("地址无效"), QStringLiteral("#f44336"));
        return;
    }
    setStatus(tr("连接中…"), QStringLiteral("#ffb300"));
    m_player->setFile(url.toString());
    m_player->play();
}

void VideoTile::stop()
{
    if (m_player) {
        m_player->stop();
    }
}

void VideoTile::setStatus(const QString &text, const QString &color)
{
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(color));
}

void VideoTile::setSelected(bool selected)
{
    m_frame->setStyleSheet(selected
        ? QStringLiteral("#tileFrame{background:#0d1b2a;} this{border:1px solid #2196f3;}")
        : QString());
    setStyleSheet(selected
        ? QStringLiteral("QWidget#VideoTile{border:2px solid #2196f3;border-radius:2px;}")
        : QStringLiteral("QWidget#VideoTile{border:1px solid #263238;border-radius:2px;}"));
}

void VideoTile::mousePressEvent(QMouseEvent *e)
{
    emit clickedTile(m_deviceId);
    QWidget::mousePressEvent(e);
}

} // namespace zp
