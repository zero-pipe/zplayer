#include "ui/evidence_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace zp {

namespace {

QString hms(double seconds)
{
    const int t = qMax(0, int(seconds + 0.5));
    return QStringLiteral("%1:%2:%3")
        .arg(t / 3600, 2, 10, QLatin1Char('0'))
        .arg((t / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(t % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

EvidenceDialog::EvidenceDialog(const QString &mediaName,
                               const QVector<EvidenceEvent> &events, QWidget *parent)
    : QDialog(parent), m_events(events)
{
    setWindowTitle(tr("证据预览与导出"));
    setObjectName(QStringLiteral("EvidenceDialog"));
    buildUi(mediaName);
}

void EvidenceDialog::buildUi(const QString &mediaName)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    auto *title = new QLabel(tr("证据清单 — %1").arg(mediaName), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    root->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("EvidenceList"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const EvidenceEvent &ev : m_events) {
        auto *item = new QListWidgetItem(m_list);
        auto *row = new QWidget(m_list);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(6, 4, 6, 4);
        hl->setSpacing(10);

        auto *check = new QCheckBox(row);
        check->setChecked(true);
        hl->addWidget(check);

        QLabel *thumb = new QLabel(row);
        thumb->setFixedSize(96, 54);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setObjectName(QStringLiteral("EvidenceThumb"));
        if (!ev.thumbSource.isEmpty() && QFileInfo::exists(ev.thumbSource)) {
            QPixmap pm(ev.thumbSource);
            thumb->setPixmap(pm.scaled(96, 54, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation));
        } else {
            thumb->setText(tr("无截图"));
        }
        hl->addWidget(thumb);

        auto *info = new QLabel(
            tr("%1 – %2\n%3 ×%4")
                .arg(hms(ev.start), hms(ev.end),
                     ev.label.isEmpty() ? QString::number(ev.classId) : ev.label)
                .arg(ev.peakCount),
            row);
        info->setObjectName(QStringLiteral("EvidenceInfo"));
        hl->addWidget(info, 1);

        item->setSizeHint(QSize(0, 66));
        m_list->setItemWidget(item, row);
    }
    root->addWidget(m_list, 1);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("EvidenceSummary"));
    root->addWidget(m_summary);

    auto *btnRow = new QHBoxLayout();
    auto *allBtn = new QPushButton(tr("全选"), this);
    auto *noneBtn = new QPushButton(tr("全不选"), this);
    connect(allBtn, &QPushButton::clicked, this, [this]() { setAllChecked(true); });
    connect(noneBtn, &QPushButton::clicked, this, [this]() { setAllChecked(false); });
    btnRow->addWidget(allBtn);
    btnRow->addWidget(noneBtn);
    btnRow->addStretch(1);

    auto *cancel = new QPushButton(tr("取消"), this);
    auto *exportBtn = new QPushButton(tr("导出选中"), this);
    exportBtn->setObjectName(QStringLiteral("PrimaryButton"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QVector<EvidenceEvent> sel = selectedEvents();
        if (!sel.isEmpty()) {
            emit exportRequested(sel);
        }
        accept();
    });
    btnRow->addWidget(cancel);
    btnRow->addWidget(exportBtn);
    root->addLayout(btnRow);

    // The checkbox is the source of truth; ticking updates the summary.
    for (int i = 0; i < m_list->count(); ++i) {
        auto *row = m_list->itemWidget(m_list->item(i));
        if (!row) {
            continue;
        }
        if (auto *check = row->findChild<QCheckBox *>()) {
            connect(check, &QCheckBox::toggled, this, [this]() { updateSummary(); });
        }
    }
    updateSummary();
}

QVector<EvidenceEvent> EvidenceDialog::selectedEvents() const
{
    QVector<EvidenceEvent> out;
    for (int i = 0; i < m_list->count() && i < m_events.size(); ++i) {
        auto *row = m_list->itemWidget(m_list->item(i));
        if (!row) {
            continue;
        }
        auto *check = row->findChild<QCheckBox *>();
        if (check && check->isChecked()) {
            out.append(m_events.at(i));
        }
    }
    return out;
}

void EvidenceDialog::setAllChecked(bool checked)
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *row = m_list->itemWidget(m_list->item(i));
        if (auto *check = row ? row->findChild<QCheckBox *>() : nullptr) {
            check->setChecked(checked);
        }
    }
}

void EvidenceDialog::updateSummary()
{
    m_summary->setText(tr("共 %1 个事件，已选 %2 个；导出包含事件列表与截图包")
                           .arg(m_events.size())
                           .arg(selectedEvents().size()));
}

} // namespace zp
