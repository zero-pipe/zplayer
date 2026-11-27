#include "ui/playback_dialog.h"

#include <QDateEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

namespace zp {

static QString fmtClock(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs).toString(QStringLiteral("HH:mm:ss"));
}

static QString fmtSize(qint64 bytes)
{
    if (bytes <= 0) return QString();
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 0);
}

PlaybackDialog::PlaybackDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("云端回放"));
    resize(460, 520);

    auto *lay = new QVBoxLayout(this);

    m_titleLabel = new QLabel(tr("通道：（未选择）"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    lay->addWidget(m_titleLabel);

    auto *queryRow = new QHBoxLayout;
    m_date = new QDateEdit(QDate::currentDate(), this);
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_queryBtn = new QPushButton(tr("查询录像"), this);
    connect(m_queryBtn, &QPushButton::clicked, this, &PlaybackDialog::onQuery);
    queryRow->addWidget(m_date, 1);
    queryRow->addWidget(m_queryBtn);
    lay->addLayout(queryRow);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onPlaySelected(); });
    lay->addWidget(m_list, 1);

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(m_status);

    auto *timeRow = new QHBoxLayout;
    m_posLabel = new QLabel(QStringLiteral("--:--:--"), this);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);
    m_slider->setEnabled(false);
    connect(m_slider, &QSlider::sliderReleased, this, &PlaybackDialog::onSliderReleased);
    m_totalLabel = new QLabel(QStringLiteral("--:--:--"), this);
    timeRow->addWidget(m_posLabel);
    timeRow->addWidget(m_slider, 1);
    timeRow->addWidget(m_totalLabel);
    lay->addLayout(timeRow);

    auto *btnRow = new QHBoxLayout;
    m_playBtn = new QPushButton(tr("播放选中片段"), this);
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &PlaybackDialog::onPlaySelected);
    m_stopBtn = new QPushButton(tr("停止"), this);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &PlaybackDialog::onStop);
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_stopBtn);
    lay->addLayout(btnRow);

    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, &PlaybackDialog::onTick);
}

void PlaybackDialog::setChannelName(const QString &name)
{
    m_titleLabel->setText(tr("通道：%1").arg(name));
}

void PlaybackDialog::setSegments(const QList<RecordingSegment> &segs)
{
    m_segs = segs;
    m_list->clear();
    for (const RecordingSegment &s : segs) {
        const QString sz = fmtSize(s.size);
        QString text = QStringLiteral("%1 - %2    %3")
                .arg(fmtClock(s.startMs), fmtClock(s.endMs),
                     s.container.isEmpty() ? QStringLiteral("录像") : s.container.toUpper());
        if (!sz.isEmpty()) text += QStringLiteral("  ·  ") + sz;
        m_list->addItem(text);
    }
    setBusy(false);
    m_playBtn->setEnabled(!segs.isEmpty());
    if (segs.isEmpty()) {
        m_status->setText(tr("该日无录像"));
        m_list->addItem(tr("（无录像片段）"));
    } else {
        m_status->setText(tr("共 %1 个片段，双击或选中后播放").arg(segs.size()));
        m_list->setCurrentRow(0);
    }
}

void PlaybackDialog::onQuery()
{
    const QDate d = m_date->date();
    const QDateTime start(d, QTime(0, 0, 0));
    const QDateTime end = start.addDays(1);
    m_status->setText(tr("查询中…"));
    setBusy(true);
    emit queryRequested(start.toMSecsSinceEpoch(), end.toMSecsSinceEpoch());
}

void PlaybackDialog::onPlaySelected()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return;
    int row = m_list->row(it);
    if (row < 0 || row >= m_segs.size()) return;
    const RecordingSegment seg = m_segs.at(row);
    m_status->setText(tr("起播中…"));
    setBusy(true);
    emit playSegmentRequested(seg.recordId, seg.startMs);
}

void PlaybackDialog::setPlaying(qlonglong recordId, qint64 segStartMs, qint64 durMs)
{
    m_recordId = recordId;
    m_segStart = segStartMs;
    m_segDuration = durMs > 0 ? durMs : 1;
    m_playBasePos = segStartMs;
    m_playBaseWall = QDateTime::currentMSecsSinceEpoch();
    m_playing = true;

    m_slider->blockSignals(true);
    m_slider->setRange(0, int(m_segDuration / 1000));
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    m_slider->setEnabled(true);

    m_totalLabel->setText(fmtClock(m_segStart + m_segDuration));
    m_stopBtn->setEnabled(true);
    m_status->setText(tr("回放中"));
    setBusy(false);
    m_tick->start();
    updatePlayhead();
}

void PlaybackDialog::setPlaybackError(const QString &msg)
{
    m_playing = false;
    m_tick->stop();
    m_status->setText(tr("错误：%1").arg(msg));
    setBusy(false);
}

void PlaybackDialog::setBusy(bool busy)
{
    m_queryBtn->setEnabled(!busy);
    m_playBtn->setEnabled(!busy && !m_segs.isEmpty());
}

void PlaybackDialog::onStop()
{
    m_playing = false;
    m_tick->stop();
    m_slider->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_status->setText(tr("已停止"));
    emit stopRequested();
}

void PlaybackDialog::onSliderReleased()
{
    if (!m_playing) return;
    const qint64 absMs = m_segStart + qint64(m_slider->value()) * 1000;
    m_playBasePos = absMs;
    m_playBaseWall = QDateTime::currentMSecsSinceEpoch();
    m_posLabel->setText(fmtClock(absMs));
    m_status->setText(tr("定位中…"));
    emit seekRequested(absMs);
}

void PlaybackDialog::onTick()
{
    updatePlayhead();
}

void PlaybackDialog::updatePlayhead()
{
    if (!m_playing) return;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_playBaseWall;
    qint64 pos = m_playBasePos + elapsed;
    const qint64 segEnd = m_segStart + m_segDuration;
    if (pos > segEnd) pos = segEnd;
    if (pos < m_segStart) pos = m_segStart;

    m_slider->blockSignals(true);
    m_slider->setValue(int((pos - m_segStart) / 1000));
    m_slider->blockSignals(false);
    m_posLabel->setText(fmtClock(pos));

    if (pos >= segEnd) {
        m_tick->stop();
        m_status->setText(tr("片段结束"));
    }
}

} // namespace zp
