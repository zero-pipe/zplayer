#include "train/training_tool_window.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "train/annotation_canvas.h"
#include "train/model_registry.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_kit.h"

namespace zp {

namespace {

using uikit::ChipBad;
using uikit::ChipMuted;
using uikit::ChipOk;
using uikit::ChipWarn;

// Item data roles used by the queue table's chip delegate.
constexpr int kRoleChipKind = Qt::UserRole + 1;
constexpr int kRoleChipText = Qt::UserRole + 2;

// Paints soft rounded chips in table cells (trigger / status columns) so the
// queue matches the prototype instead of showing bare text.
class ChipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const QString kind = idx.data(kRoleChipKind).toString();
        if (kind.isEmpty()) {
            QStyledItemDelegate::paint(p, opt, idx);
            return;
        }
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        o.text.clear();
        QStyledItemDelegate::paint(p, o, idx);   // base bg (selection / hover)

        const ThemeTokens &tk = ThemeManager::instance().tokens();
        QColor fg = tk.muted, bg = tk.bgInput;
        if (kind == QLatin1String("ok")) {
            fg = tk.success;
            bg = tk.successSoft;
        } else if (kind == QLatin1String("warn")) {
            fg = tk.warning;
            bg = tk.warningSoft;
        } else if (kind == QLatin1String("bad")) {
            fg = tk.danger;
            bg = tk.dangerSoft;
        } else if (kind == QLatin1String("info")) {
            fg = tk.accent;
            bg = tk.accentSoft;
        }

        QFont f = opt.font;
        f.setPixelSize(10);
        f.setBold(true);
        const QString text = idx.data(kRoleChipText).toString();
        const QFontMetrics fm(f);
        const QRect r = opt.rect;
        const int tw = fm.horizontalAdvance(text) + 12;
        const int th = fm.height() + 4;
        const QRectF chip(r.left() + 8, r.center().y() - th / 2.0, tw, th);

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setFont(f);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(fg);
        p->drawText(chip, Qt::AlignCenter, text);
        p->restore();
    }
};

QLabel *pageTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("PageTitle"));
    return l;
}

QLabel *subLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("PageSub"));
    return l;
}

QLabel *cardTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("CardTitle"));
    return l;
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("FieldLabel"));
    return l;
}

QColor classColor(int id)
{
    switch (id) {
    case 0: return QColor(0x25, 0x63, 0xEB);
    case 2: case 3: case 5: case 7: return QColor(0x16, 0xA3, 0x4A);
    case 16: return QColor(0xF5, 0x9E, 0x0B);
    default: return QColor(0x94, 0xA3, 0xB8);
    }
}

QIcon colorDot(const QColor &color)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(2, 2, 8, 8);
    return QIcon(pm);
}

QStringList splitCommand(const QString &cmd)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    for (const QChar c : cmd) {
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            continue;
        }
        if (c.isSpace() && !inQuote) {
            if (!cur.isEmpty()) {
                out.append(cur);
                cur.clear();
            }
            continue;
        }
        cur.append(c);
    }
    if (!cur.isEmpty()) {
        out.append(cur);
    }
    return out;
}

} // namespace

TrainingToolWindow::TrainingToolWindow(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("TrainingTool"));
    setWindowTitle(tr("Zeroo AI 训练工具 · 本地"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    setMinimumSize(1080, 720);
    buildUi();
    reload();
}

// ---------------------------------------------------------------------------
// Shell: left nav + stacked pages
// ---------------------------------------------------------------------------
void TrainingToolWindow::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildNav());

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildQueuePage());     // 0
    m_stack->addWidget(buildAnnotatePage());  // 1
    m_stack->addWidget(buildQcPage());        // 2
    m_stack->addWidget(buildTrainPage());     // 3
    m_stack->addWidget(buildEvalPage());      // 4
    m_stack->addWidget(buildPublishPage());   // 5
    root->addWidget(m_stack, 1);

    // Annotation page keyboard: A/D navigate, 1-4 pick a class.
    auto pageShortcut = [this](const QKeySequence &seq, const std::function<void()> &fn) {
        auto *sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, fn]() {
            if (m_page == 1) {
                fn();
            }
        });
    };
    pageShortcut(QKeySequence(Qt::Key_A), [this]() {
        if (m_current > 0) {
            openRecord(m_current - 1);
        }
    });
    pageShortcut(QKeySequence(Qt::Key_D), [this]() {
        if (m_current + 1 < m_records.size()) {
            openRecord(m_current + 1);
        }
    });
    const Qt::Key digitKeys[4] = {Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4};
    for (int i = 0; i < 4; ++i) {
        pageShortcut(QKeySequence(digitKeys[i]), [this, i]() {
            if (auto *btn = m_classGroup ? m_classGroup->button(i) : nullptr) {
                btn->setChecked(true);
                const int id = btn->property("classId").toInt();
                const QString label = btn->property("classLabel").toString();
                m_canvas->setCurrentClass(id, label);
                m_canvas->setSelectedClass(id, label);
                refreshInspector();
            }
        });
    }

    showPage(0);
}

QWidget *TrainingToolWindow::buildNav()
{
    m_nav = new QWidget(this);
    m_nav->setObjectName(QStringLiteral("TrainNav"));
    m_nav->setFixedWidth(196);

    auto *v = new QVBoxLayout(m_nav);
    v->setContentsMargins(12, 14, 12, 14);
    v->setSpacing(4);

    struct NavDef { icons::Glyph glyph; QString text; };
    const NavDef defs[] = {
        {icons::Queue, tr("待标注")},
        {icons::Annotate, tr("标注")},
        {icons::CheckBadge, tr("质检")},
        {icons::Bolt, tr("训练")},
        {icons::Chart, tr("评估")},
        {icons::Upload, tr("发布")},
    };

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    const ThemeTokens &tk = ThemeManager::instance().tokens();
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(defs[i].text, m_nav);
        b->setObjectName(QStringLiteral("NavItem"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        QIcon ic;
        ic.addPixmap(icons::glyph(defs[i].glyph, tk.text2, 18), QIcon::Normal, QIcon::Off);
        ic.addPixmap(icons::glyph(defs[i].glyph, tk.accent, 18), QIcon::Normal, QIcon::On);
        b->setIcon(ic);
        b->setIconSize(QSize(18, 18));

        auto *lay = new QHBoxLayout(b);
        lay->setContentsMargins(0, 0, 4, 0);
        lay->setSpacing(6);
        lay->addStretch(1);
        if (i == 0) {
            m_navQueueBadge = new QLabel(b);
            m_navQueueBadge->setObjectName(QStringLiteral("NavBadge"));
            m_navQueueBadge->hide();
            lay->addWidget(m_navQueueBadge);
        } else if (i == 2) {
            m_navQcBadge = new QLabel(b);
            m_navQcBadge->setObjectName(QStringLiteral("NavBadge"));
            m_navQcBadge->hide();
            lay->addWidget(m_navQcBadge);
        } else if (i == 3) {
            auto *dot = new QLabel(b);
            dot->setObjectName(QStringLiteral("NavDot"));
            lay->addWidget(dot);
        }

        m_navGroup->addButton(b, i);
        m_navBtns.append(b);
        v->addWidget(b);
    }
    v->addStretch(1);

    // Device / backend footer.
    auto *foot = new QFrame(m_nav);
    foot->setObjectName(QStringLiteral("NavFooter"));
    auto *fv = new QVBoxLayout(foot);
    fv->setContentsMargins(10, 8, 10, 8);
    fv->setSpacing(2);
    auto *key = new QLabel(tr("本机训练"), foot);
    key->setObjectName(QStringLiteral("NavFootKey"));
    m_navFootVal = new QLabel(tr("CPU"), foot);
    m_navFootVal->setObjectName(QStringLiteral("NavFootVal"));
    fv->addWidget(key);
    fv->addWidget(m_navFootVal);
    v->addWidget(foot);

    connect(m_navGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this,
            &TrainingToolWindow::showPage);
    return m_nav;
}

// ---------------------------------------------------------------------------
// Page 0: queue
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildQueuePage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);

    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    head->addWidget(pageTitle(tr("待标注队列"), page));
    m_queueInfo = subLabel(QString(), page);
    head->addWidget(m_queueInfo);
    head->addStretch(1);
    m_queueSearch = new QLineEdit(page);
    m_queueSearch->setObjectName(QStringLiteral("SearchInput"));
    m_queueSearch->setPlaceholderText(tr("搜索样本…"));
    m_queueSearch->setFixedWidth(170);
    head->addWidget(m_queueSearch);
    auto *refresh = new QPushButton(tr("刷新"), page);
    refresh->setObjectName(QStringLiteral("GhostBtn"));
    head->addWidget(refresh);
    v->addLayout(head);

    m_queue = new QTableWidget(page);
    m_queue->setObjectName(QStringLiteral("QueueTable"));
    m_queue->setColumnCount(6);
    m_queue->setHorizontalHeaderLabels(
        {QString(), tr("文件名"), tr("触发类型"), tr("置信度"), tr("状态"), QString()});
    m_queue->verticalHeader()->setVisible(false);
    m_queue->verticalHeader()->setDefaultSectionSize(36);
    m_queue->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queue->setSelectionMode(QAbstractItemView::SingleSelection);
    m_queue->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queue->setShowGrid(false);
    m_queue->setItemDelegate(new ChipDelegate(m_queue));
    auto *hh = m_queue->horizontalHeader();
    hh->setSectionsClickable(false);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setSectionResizeMode(0, QHeaderView::Fixed);
    m_queue->setColumnWidth(0, 34);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    hh->setSectionResizeMode(2, QHeaderView::Fixed);
    m_queue->setColumnWidth(2, 106);
    hh->setSectionResizeMode(3, QHeaderView::Fixed);
    m_queue->setColumnWidth(3, 82);
    hh->setSectionResizeMode(4, QHeaderView::Fixed);
    m_queue->setColumnWidth(4, 92);
    hh->setSectionResizeMode(5, QHeaderView::Fixed);
    m_queue->setColumnWidth(5, 104);
    v->addWidget(m_queue, 1);

    auto *foot = new QHBoxLayout();
    foot->setSpacing(8);
    auto *clearSel = new QPushButton(tr("清空选中"), page);
    clearSel->setObjectName(QStringLiteral("GhostBtn"));
    m_annotateBtn = new QPushButton(tr("标注选中样本 →"), page);
    m_annotateBtn->setObjectName(QStringLiteral("PrimaryButton"));
    m_annotateBtn->setCursor(Qt::PointingHandCursor);
    foot->addWidget(clearSel);
    foot->addStretch(1);
    foot->addWidget(m_annotateBtn);
    v->addLayout(foot);

    connect(refresh, &QPushButton::clicked, this, &TrainingToolWindow::reload);
    connect(m_queueSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < m_queue->rowCount(); ++row) {
            const QString name = m_queue->item(row, 1) ? m_queue->item(row, 1)->text() : QString();
            const QString reason = m_queue->item(row, 2)
                                       ? m_queue->item(row, 2)->data(kRoleChipText).toString()
                                       : QString();
            m_queue->setRowHidden(row,
                                  !text.isEmpty() && !name.contains(text, Qt::CaseInsensitive)
                                      && !reason.contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_queue, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (!m_loadingQueue) {
            updateAnnotateButtons();
        }
    });
    connect(m_queue, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { openRecord(row); });
    connect(clearSel, &QPushButton::clicked, this, [this]() {
        m_loadingQueue = true;
        for (int row = 0; row < m_queue->rowCount(); ++row) {
            if (QTableWidgetItem *it = m_queue->item(row, 0)) {
                it->setCheckState(Qt::Unchecked);
            }
        }
        m_loadingQueue = false;
        updateAnnotateButtons();
    });
    connect(m_annotateBtn, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < m_queue->rowCount(); ++row) {
            QTableWidgetItem *it = m_queue->item(row, 0);
            if (it && it->checkState() == Qt::Checked) {
                openRecord(row);
                return;
            }
        }
        const int row = m_queue->currentRow();
        if (row >= 0) {
            openRecord(row);
        }
    });

    return page;
}

int TrainingToolWindow::selectedQueueRowsCount() const
{
    int n = 0;
    if (!m_queue) {
        return n;
    }
    for (int row = 0; row < m_queue->rowCount(); ++row) {
        QTableWidgetItem *it = m_queue->item(row, 0);
        if (it && it->checkState() == Qt::Checked) {
            ++n;
        }
    }
    return n;
}

void TrainingToolWindow::updateAnnotateButtons()
{
    if (!m_annotateBtn) {
        return;
    }
    const int n = selectedQueueRowsCount();
    m_annotateBtn->setText(n > 0 ? tr("标注选中样本 (%1) →").arg(n)
                                 : tr("标注选中样本 →"));
    m_annotateBtn->setEnabled(m_queue && m_queue->rowCount() > 0);
}

void TrainingToolWindow::reloadQueue()
{
    if (!m_queue) {
        return;
    }
    m_loadingQueue = true;
    m_queue->setRowCount(0);
    const ThemeTokens tk = ThemeManager::instance().tokens();
    for (int i = 0; i < m_records.size(); ++i) {
        const SampleRecord &r = m_records[i];
        int score = 0;
        for (const AnalysisBox &b : r.boxes) {
            score = qMax(score, int(b.score * 100));
        }

        m_queue->insertRow(i);

        auto *c0 = new QTableWidgetItem();
        c0->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        c0->setCheckState(r.done ? Qt::Unchecked : Qt::Checked);
        c0->setTextAlignment(Qt::AlignCenter);
        m_queue->setItem(i, 0, c0);

        auto *c1 = new QTableWidgetItem(r.channel);
        QFont f = c1->font();
        f.setBold(true);
        c1->setFont(f);
        if (r.done) {
            c1->setForeground(tk.muted);
        }
        m_queue->setItem(i, 1, c1);

        auto *c2 = new QTableWidgetItem();
        c2->setData(kRoleChipKind, QStringLiteral("warn"));
        c2->setData(kRoleChipText, r.reason.isEmpty() ? tr("manual") : r.reason);
        if (r.done) {
            c2->setData(kRoleChipKind, QStringLiteral("muted"));
        }
        m_queue->setItem(i, 2, c2);

        auto *c3 = new QTableWidgetItem(score > 0 ? QStringLiteral("%1%").arg(score)
                                                   : QString::fromUtf8("—"));
        c3->setTextAlignment(Qt::AlignCenter);
        QFont cf = c3->font();
        cf.setBold(true);
        c3->setFont(cf);
        QColor confColor = tk.warning;
        if (score >= 70) {
            confColor = tk.success;
        } else if (score > 0 && score < 40) {
            confColor = tk.danger;
        } else if (score == 0) {
            confColor = tk.muted;
        }
        c3->setForeground(confColor);
        m_queue->setItem(i, 3, c3);

        auto *c4 = new QTableWidgetItem();
        c4->setData(kRoleChipKind, r.done ? QStringLiteral("info") : QStringLiteral("warn"));
        c4->setData(kRoleChipText, r.done ? tr("已标注") : tr("待处理"));
        m_queue->setItem(i, 4, c4);

        auto *c5 = new QTableWidgetItem(tr("进入标注 →"));
        c5->setForeground(tk.accent);
        m_queue->setItem(i, 5, c5);
    }
    m_loadingQueue = false;
    m_queueInfo->setText(tr("（%1 条样本）").arg(m_records.size()));
    updateAnnotateButtons();
}

// ---------------------------------------------------------------------------
// Page 1: annotation
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildAnnotatePage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Sub header: file + reason chip + position.
    auto *bar = new QWidget(page);
    bar->setObjectName(QStringLiteral("AnnoBar"));
    bar->setFixedHeight(40);
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(14, 0, 14, 0);
    bh->setSpacing(8);
    m_annoName = new QLabel(QString::fromUtf8("—"), bar);
    m_annoName->setObjectName(QStringLiteral("AnnoName"));
    m_annoReason = uikit::makeChip(QString::fromUtf8("—"), ChipWarn, bar);
    m_annoIndex = new QLabel(QString::fromUtf8("— / —"), bar);
    m_annoIndex->setObjectName(QStringLiteral("AnnoIndex"));
    bh->addWidget(m_annoName);
    bh->addWidget(m_annoReason);
    bh->addStretch(1);
    bh->addWidget(m_annoIndex);
    v->addWidget(bar);

    // Canvas + inspector.
    auto *body = new QWidget(page);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_canvas = new AnnotationCanvas(body);
    bodyLay->addWidget(m_canvas, 1);

    auto *inspector = new QWidget(body);
    inspector->setObjectName(QStringLiteral("AnnoInspector"));
    inspector->setFixedWidth(212);
    auto *iv = new QVBoxLayout(inspector);
    iv->setContentsMargins(10, 10, 10, 10);
    iv->setSpacing(8);

    auto *inspHead = new QHBoxLayout();
    m_boxCount = cardTitle(tr("目标框 (0)"), inspector);
    auto *addBox = new QPushButton(tr("+ 手动添加"), inspector);
    addBox->setObjectName(QStringLiteral("LinkBtn"));
    addBox->setCursor(Qt::PointingHandCursor);
    inspHead->addWidget(m_boxCount);
    inspHead->addStretch(1);
    inspHead->addWidget(addBox);
    iv->addLayout(inspHead);

    m_boxList = new QListWidget(inspector);
    m_boxList->setObjectName(QStringLiteral("BoxList"));
    iv->addWidget(m_boxList, 1);

    iv->addWidget(fieldLabel(tr("类别选择器"), inspector));
    auto *clsHead = new QHBoxLayout();
    clsHead->setSpacing(6);
    auto *manageBtn = new QPushButton(tr("管理类别…"), inspector);
    manageBtn->setObjectName(QStringLiteral("LinkBtn"));
    manageBtn->setCursor(Qt::PointingHandCursor);
    clsHead->addWidget(fieldLabel(tr("类别"), inspector));
    clsHead->addStretch(1);
    clsHead->addWidget(manageBtn);
    iv->addLayout(clsHead);

    m_classGroup = new QButtonGroup(this);
    m_classGroup->setExclusive(true);
    m_classGrid = new QGridLayout();
    m_classGrid->setSpacing(4);
    iv->addLayout(m_classGrid);
    rebuildClassButtons();

    bodyLay->addWidget(inspector);
    v->addWidget(body, 1);

    // Footer: prev / next / save.
    auto *foot = new QWidget(page);
    foot->setObjectName(QStringLiteral("AnnoFoot"));
    foot->setFixedHeight(48);
    auto *fh = new QHBoxLayout(foot);
    fh->setContentsMargins(14, 0, 14, 0);
    fh->setSpacing(8);
    auto *prev = new QPushButton(tr("← 上一张 (A)"), foot);
    prev->setObjectName(QStringLiteral("GhostBtn"));
    auto *next = new QPushButton(tr("下一张 (D) →"), foot);
    next->setObjectName(QStringLiteral("GhostBtn"));
    auto *save = new QPushButton(tr("保存并下一张"), foot);
    save->setObjectName(QStringLiteral("PrimaryButton"));
    save->setCursor(Qt::PointingHandCursor);
    fh->addWidget(prev);
    fh->addWidget(next);
    fh->addWidget(subLabel(tr("拖动移动 · 双击空白加框 · Del 删除"), foot));
    fh->addStretch(1);
    fh->addWidget(save);
    v->addWidget(foot);

    connect(prev, &QPushButton::clicked, this, [this]() {
        if (m_current > 0) {
            openRecord(m_current - 1);
        }
    });
    connect(next, &QPushButton::clicked, this, [this]() {
        if (m_current + 1 < m_records.size()) {
            openRecord(m_current + 1);
        }
    });
    connect(save, &QPushButton::clicked, this, &TrainingToolWindow::saveCurrent);
    connect(addBox, &QPushButton::clicked, this, [this]() { m_canvas->addBoxCentered(); });

    connect(m_canvas, &AnnotationCanvas::boxesChanged, this,
            &TrainingToolWindow::refreshInspector);
    connect(m_canvas, &AnnotationCanvas::selectionChanged, this,
            &TrainingToolWindow::syncInspectorSelection);
    connect(m_boxList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_syncingInspector && row >= 0) {
            m_canvas->selectBox(row);
        }
    });
    connect(m_classGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) {
        if (auto *btn = m_classGroup->checkedButton()) {
            const int id = btn->property("classId").toInt();
            const QString label = btn->property("classLabel").toString();
            m_canvas->setCurrentClass(id, label);
            m_canvas->setSelectedClass(id, label);
            refreshInspector();
        }
    });
    connect(manageBtn, &QPushButton::clicked, this, &TrainingToolWindow::openClassManager);

    return page;
}

void TrainingToolWindow::rebuildClassButtons()
{
    if (!m_classGrid || !m_classGroup) {
        return;
    }
    for (QAbstractButton *b : m_classGroup->buttons()) {
        m_classGroup->removeButton(b);
        if (QWidget *w = qobject_cast<QWidget *>(b)) {
            w->deleteLater();
        }
    }
    while (QLayoutItem *it = m_classGrid->takeAt(0)) {
        delete it;   // widgets were scheduled for deletion above
    }
    QWidget *host = m_classGrid->parentWidget();
    const QVector<ClassDef> cls = DatasetStore::instance().classes();
    int i = 0;
    for (const ClassDef &c : cls) {
        auto *b = new QToolButton(host);
        b->setObjectName(QStringLiteral("ClassBtn"));
        const QString label = c.zh.isEmpty() ? c.name : c.zh;
        b->setText(QStringLiteral("%1   [%2]").arg(label).arg(i + 1));
        b->setProperty("classId", c.id);
        b->setProperty("classLabel", label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_classGroup->addButton(b, i);
        m_classGrid->addWidget(b, i / 2, i % 2);
        ++i;
    }
    if (i > 0) {
        auto *first = qobject_cast<QToolButton *>(m_classGroup->button(0));
        first->setChecked(true);
        if (m_canvas) {
            m_canvas->setCurrentClass(first->property("classId").toInt(),
                                      first->property("classLabel").toString());
        }
    }
}

void TrainingToolWindow::openClassManager()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("管理类别"));
    dlg.setMinimumWidth(460);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(8);
    auto *hint = new QLabel(tr("类别会写进导出数据集与训练出的模型。中文名用于界面与检测框显示。"), &dlg);
    hint->setObjectName(QStringLiteral("VsHint"));
    hint->setWordWrap(true);
    v->addWidget(hint);

    auto *list = new QListWidget(&dlg);
    list->setObjectName(QStringLiteral("BoxList"));
    list->setMinimumHeight(150);
    v->addWidget(list, 1);

    auto refresh = [list]() {
        list->clear();
        for (const ClassDef &c : DatasetStore::instance().classes()) {
            const QString display = c.zh.isEmpty() ? c.name : c.zh;
            auto *it = new QListWidgetItem(
                QStringLiteral("%1   ·   %2").arg(display, c.name), list);
            it->setData(Qt::UserRole, c.id);
        }
    };
    refresh();

    auto *err = new QLabel(&dlg);
    err->setObjectName(QStringLiteral("ErrorLabel"));
    v->addWidget(err);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(6);
    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setObjectName(QStringLiteral("ParamBox"));
    nameEdit->setPlaceholderText(tr("英文名，如 helmet"));
    auto *zhEdit = new QLineEdit(&dlg);
    zhEdit->setObjectName(QStringLiteral("ParamBox"));
    zhEdit->setPlaceholderText(tr("中文名，如 安全帽"));
    auto *addBtn = new QPushButton(tr("添加"), &dlg);
    addBtn->setObjectName(QStringLiteral("PrimaryButton"));
    inputRow->addWidget(nameEdit, 1);
    inputRow->addWidget(zhEdit, 1);
    inputRow->addWidget(addBtn);
    v->addLayout(inputRow);

    auto *bottom = new QHBoxLayout();
    auto *delBtn = new QPushButton(tr("删除选中"), &dlg);
    delBtn->setObjectName(QStringLiteral("GhostBtn"));
    auto *closeBtn = new QPushButton(tr("关闭"), &dlg);
    closeBtn->setObjectName(QStringLiteral("GhostBtn"));
    bottom->addWidget(delBtn);
    bottom->addStretch(1);
    bottom->addWidget(closeBtn);
    v->addLayout(bottom);

    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        QString e;
        const QString zh = zhEdit->text().trimmed();
        const QString name = nameEdit->text().trimmed();
        if (DatasetStore::instance().addClass(name, zh, &e) < 0) {
            err->setText(e);
            return;
        }
        err->clear();
        nameEdit->clear();
        zhEdit->clear();
        refresh();
    });
    connect(delBtn, &QPushButton::clicked, &dlg, [&]() {
        QListWidgetItem *it = list->currentItem();
        if (!it) {
            err->setText(tr("先选中要删除的类别"));
            return;
        }
        const int id = it->data(Qt::UserRole).toInt();
        for (const SampleRecord &r : DatasetStore::instance().load()) {
            for (const AnalysisBox &b : r.boxes) {
                if (b.classId == id) {
                    err->setText(tr("该类别已被样本使用，不能删除"));
                    return;
                }
            }
        }
        QVector<ClassDef> cls = DatasetStore::instance().classes();
        if (cls.size() <= 1) {
            err->setText(tr("至少保留一个类别"));
            return;
        }
        for (int i = 0; i < cls.size(); ++i) {
            if (cls[i].id == id) {
                cls.remove(i);
                break;
            }
        }
        DatasetStore::instance().setClasses(cls);
        err->clear();
        refresh();
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();

    rebuildClassButtons();
    refreshInspector();
}

void TrainingToolWindow::refreshInspector()
{
    if (!m_canvas || !m_boxList) {
        return;
    }
    const QVector<AnalysisBox> boxes = m_canvas->boxes();
    m_boxCount->setText(tr("目标框 (%1)").arg(boxes.size()));

    const int sel = m_canvas->selectedIndex();
    m_syncingInspector = true;
    m_boxList->clear();
    for (int i = 0; i < boxes.size(); ++i) {
        const AnalysisBox &b = boxes[i];
        const QColor col = (i == sel) ? QColor(0x25, 0x63, 0xEB) : classColor(b.classId);
        QString name = DatasetStore::instance().className(b.classId);
        if (name.isEmpty()) {
            name = b.label.isEmpty() ? QStringLiteral("#%1").arg(b.classId) : b.label;
        }
        QString text = name;
        if (b.score > 0.f) {
            text += QStringLiteral("   %1%").arg(int(b.score * 100));
        }
        auto *it = new QListWidgetItem(colorDot(col), text, m_boxList);
        it->setData(Qt::UserRole, i);
    }
    m_boxList->setCurrentRow(sel);
    m_syncingInspector = false;
}

void TrainingToolWindow::syncInspectorSelection(int index)
{
    if (!m_boxList) {
        return;
    }
    m_syncingInspector = true;
    m_boxList->setCurrentRow(index);
    m_syncingInspector = false;
    // Re-tint dots so the selected row's dot is accent-coloured.
    refreshInspector();
}

// ---------------------------------------------------------------------------
// Page 2: quality check
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildQcPage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);

    auto *head = new QHBoxLayout();
    head->addWidget(pageTitle(tr("数据集质检"), page));
    head->addStretch(1);
    m_qcBadge = uikit::makeChip(tr("—"), ChipWarn, page);
    head->addWidget(m_qcBadge);
    v->addLayout(head);

    auto *kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(12);
    auto makeKpi = [page](const QString &label, QLabel **val) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("PanelCard"));
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(14, 10, 14, 10);
        cv->setSpacing(2);
        auto *t = new QLabel(label, card);
        t->setObjectName(QStringLiteral("KpiTitle"));
        auto *n = new QLabel(QStringLiteral("0"), card);
        n->setObjectName(QStringLiteral("KpiValue"));
        cv->addWidget(t);
        cv->addWidget(n);
        *val = n;
        return card;
    };
    kpiRow->addWidget(makeKpi(tr("总样本数"), &m_kpiTotal));
    kpiRow->addWidget(makeKpi(tr("已标注"), &m_kpiDone));
    kpiRow->addWidget(makeKpi(tr("待标注"), &m_kpiTodo));
    kpiRow->addWidget(makeKpi(tr("目标框总数"), &m_kpiBoxes));
    v->addLayout(kpiRow);

    auto *dist = new QFrame(page);
    dist->setObjectName(QStringLiteral("PanelCard"));
    auto *dv = new QVBoxLayout(dist);
    dv->setContentsMargins(14, 12, 14, 12);
    dv->setSpacing(10);
    dv->addWidget(cardTitle(tr("类别分布"), dist));
    m_distHost = new QVBoxLayout();
    m_distHost->setSpacing(10);
    dv->addLayout(m_distHost);
    v->addWidget(dist);
    v->addStretch(1);

    m_qcPath = subLabel(QString(), page);
    m_qcPath->setWordWrap(true);
    v->addWidget(m_qcPath);

    auto *foot = new QHBoxLayout();
    foot->setSpacing(8);
    auto *refresh = new QPushButton(tr("刷新"), page);
    refresh->setObjectName(QStringLiteral("GhostBtn"));
    auto *dirBtn = new QPushButton(tr("数据集目录…"), page);
    dirBtn->setObjectName(QStringLiteral("GhostBtn"));
    auto *exp = new QPushButton(tr("导出数据集并开始训练 →"), page);
    exp->setObjectName(QStringLiteral("PrimaryButton"));
    exp->setCursor(Qt::PointingHandCursor);
    foot->addWidget(refresh);
    foot->addWidget(dirBtn);
    foot->addStretch(1);
    foot->addWidget(exp);
    v->addLayout(foot);

    connect(refresh, &QPushButton::clicked, this, &TrainingToolWindow::refreshQc);
    connect(dirBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, tr("选择数据集目录"), DatasetStore::instance().root());
        if (!d.isEmpty()) {
            DatasetStore::instance().setRoot(d);
            reload();
            refreshQc();
        }
    });
    connect(exp, &QPushButton::clicked, this, [this]() {
        exportDataset();
        showPage(3);
    });

    return page;
}

// ---------------------------------------------------------------------------
// Page 3: training
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildTrainPage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);

    v->addWidget(pageTitle(tr("本地 GPU 训练控制台"), page));

    // Parameter card + command strip.
    auto *paramsCard = new QFrame(page);
    paramsCard->setObjectName(QStringLiteral("PanelCard"));
    auto *pv = new QVBoxLayout(paramsCard);
    pv->setContentsMargins(14, 12, 14, 12);
    pv->setSpacing(10);

    QSettings st;
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    auto addParam = [&](int col, const QString &label, QWidget *w) {
        auto *cell = new QWidget(paramsCard);
        auto *cv = new QVBoxLayout(cell);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(4);
        cv->addWidget(fieldLabel(label, cell));
        cv->addWidget(w);
        grid->addWidget(cell, 0, col);
    };
    m_epochs = new QSpinBox(paramsCard);
    m_epochs->setObjectName(QStringLiteral("ParamBox"));
    m_epochs->setRange(1, 2000);
    m_epochs->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_epochs->setValue(st.value(QStringLiteral("ai/epochs"), 200).toInt());

    m_imgsz = new QComboBox(paramsCard);
    m_imgsz->setObjectName(QStringLiteral("ParamCombo"));
    m_imgsz->addItem(QStringLiteral("640 x 640"), QStringLiteral("640"));
    m_imgsz->addItem(QStringLiteral("960 x 960"), QStringLiteral("960"));
    m_imgsz->addItem(QStringLiteral("1280 x 1280"), QStringLiteral("1280"));
    const QString savedSize = st.value(QStringLiteral("ai/imgsz"), QStringLiteral("960")).toString();
    m_imgsz->setCurrentIndex(qMax(0, m_imgsz->findData(savedSize)));

    m_batch = new QSpinBox(paramsCard);
    m_batch->setObjectName(QStringLiteral("ParamBox"));
    m_batch->setRange(-1, 256);
    m_batch->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_batch->setValue(st.value(QStringLiteral("ai/batch"), 16).toInt());
    m_batch->setToolTip(tr("-1 表示由训练脚本自动选择"));

    m_device = new QComboBox(paramsCard);
    m_device->setObjectName(QStringLiteral("ParamCombo"));
    m_device->addItem(tr("CPU"), QStringLiteral("cpu"));
    m_device->addItem(tr("GPU 0 (CUDA)"), QStringLiteral("0"));
    const QString dev = st.value(QStringLiteral("ai/device"), QStringLiteral("cpu")).toString();
    m_device->setCurrentIndex(qMax(0, m_device->findData(dev)));

    addParam(0, tr("Epochs"), m_epochs);
    addParam(1, tr("Image Size"), m_imgsz);
    addParam(2, tr("Batch Size"), m_batch);
    addParam(3, tr("Device"), m_device);
    pv->addLayout(grid);

    auto *cmdRow = new QHBoxLayout();
    cmdRow->setSpacing(6);
    m_trainCmd = new QLineEdit(paramsCard);
    m_trainCmd->setObjectName(QStringLiteral("CmdBox"));
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("tools/train/train.py"));
    m_trainCmd->setText(QStringLiteral(
        "python \"%1\" --data {data} --epochs {epochs} --imgsz {imgsz} --batch {batch} --device {device} --out {out} --metrics {metrics}")
                            .arg(script));
    const QString savedCmd = st.value(QStringLiteral("ai/trainCmd")).toString();
    if (!savedCmd.isEmpty()) {
        m_trainCmd->setText(savedCmd);
    }
    m_trainCmd->setCursorPosition(0);   // show the command from its start
    auto *copyBtn = new QPushButton(tr("复制"), paramsCard);
    copyBtn->setObjectName(QStringLiteral("LinkBtn"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    cmdRow->addWidget(m_trainCmd, 1);
    cmdRow->addWidget(copyBtn);
    pv->addWidget(fieldLabel(
        tr("训练命令（{data}/{epochs}/{imgsz}/{batch}/{device}/{out}/{metrics} 自动替换）"),
        paramsCard));
    pv->addLayout(cmdRow);
    v->addWidget(paramsCard);

    // CTA + progress.
    auto *cta = new QFrame(page);
    cta->setObjectName(QStringLiteral("PanelCard"));
    auto *cv = new QVBoxLayout(cta);
    cv->setContentsMargins(14, 12, 14, 12);
    cv->setSpacing(8);
    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    m_startBtn = new QPushButton(tr("开始训练"), cta);
    m_startBtn->setObjectName(QStringLiteral("PrimaryButton"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setIcon(QIcon(icons::glyph(icons::Bolt, QColor(Qt::white), 14)));
    m_trainStatus = subLabel(tr("准备就绪"), cta);
    m_trainPct = new QLabel(QStringLiteral("0%"), cta);
    m_trainPct->setObjectName(QStringLiteral("MonoValue"));
    row->addWidget(m_startBtn);
    row->addWidget(m_trainStatus);
    row->addStretch(1);
    row->addWidget(m_trainPct);
    cv->addLayout(row);
    m_trainProg = new QProgressBar(cta);
    m_trainProg->setObjectName(QStringLiteral("TrainProgress"));
    m_trainProg->setRange(0, 100);
    m_trainProg->setValue(0);
    m_trainProg->setTextVisible(false);
    cv->addWidget(m_trainProg);
    v->addWidget(cta);

    // Console.
    auto *console = new QFrame(page);
    console->setObjectName(QStringLiteral("ConsoleCard"));
    auto *cl = new QVBoxLayout(console);
    cl->setContentsMargins(12, 8, 12, 10);
    cl->setSpacing(6);
    auto *ch = new QHBoxLayout();
    auto *head = new QLabel(QStringLiteral("stdout / stderr"), console);
    head->setObjectName(QStringLiteral("ConsoleHead"));
    m_consoleBadge = new QLabel(tr("● IDLE"), console);
    m_consoleBadge->setObjectName(QStringLiteral("ConsoleBadge"));
    m_consoleBadge->setProperty("idle", true);
    ch->addWidget(head);
    ch->addStretch(1);
    ch->addWidget(m_consoleBadge);
    cl->addLayout(ch);
    m_trainLog = new QPlainTextEdit(console);
    m_trainLog->setObjectName(QStringLiteral("TrainLog"));
    m_trainLog->setReadOnly(true);
    cl->addWidget(m_trainLog, 1);
    v->addWidget(console, 1);

    connect(m_startBtn, &QPushButton::clicked, this, &TrainingToolWindow::startTraining);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_trainCmd->text());
    });
    auto updateFoot = [this]() {
        if (m_navFootVal && m_device) {
            m_navFootVal->setText(m_device->currentText());
        }
    };
    connect(m_device, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateFoot](int) { updateFoot(); });
    updateFoot();

    return page;
}

void TrainingToolWindow::setConsoleState(bool running)
{
    if (!m_consoleBadge) {
        return;
    }
    m_consoleBadge->setText(running ? tr("● RUNNING") : tr("● IDLE"));
    m_consoleBadge->setProperty("idle", !running);
    m_consoleBadge->style()->unpolish(m_consoleBadge);
    m_consoleBadge->style()->polish(m_consoleBadge);
}

// ---------------------------------------------------------------------------
// Page 4: evaluation
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildEvalPage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);
    v->addWidget(pageTitle(tr("模型评估指标"), page));

    auto *kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(12);
    auto makeEvalKpi = [page](const QString &label, QLabel **val) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("PanelCard"));
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(14, 10, 14, 10);
        cv->setSpacing(2);
        auto *t = new QLabel(label, card);
        t->setObjectName(QStringLiteral("KpiTitle"));
        auto *n = new QLabel(QString::fromUtf8("—"), card);
        n->setObjectName(QStringLiteral("KpiValue"));
        cv->addWidget(t);
        cv->addWidget(n);
        *val = n;
        return card;
    };
    kpiRow->addWidget(makeEvalKpi(tr("mAP@0.5"), &m_evMap50));
    kpiRow->addWidget(makeEvalKpi(tr("mAP@0.5:0.95"), &m_evMap5095));
    kpiRow->addWidget(makeEvalKpi(tr("Precision"), &m_evPrec));
    kpiRow->addWidget(makeEvalKpi(tr("Recall"), &m_evRecall));
    v->addLayout(kpiRow);

    m_evDelta = subLabel(QString(), page);
    v->addWidget(m_evDelta);

    auto *cmp = new QFrame(page);
    cmp->setObjectName(QStringLiteral("PanelCard"));
    auto *cv = new QVBoxLayout(cmp);
    cv->setContentsMargins(14, 12, 14, 12);
    cv->setSpacing(8);
    cv->addWidget(cardTitle(tr("新旧模型精度对比 (mAP@0.5)"), cmp));
    auto makeCmpRow = [&](QLabel **label, QProgressBar **bar) {
        auto *row = new QWidget(cmp);
        auto *rv = new QVBoxLayout(row);
        rv->setContentsMargins(0, 0, 0, 0);
        rv->setSpacing(4);
        auto *top = new QHBoxLayout();
        *label = new QLabel(QString::fromUtf8("—"), row);
        (*label)->setObjectName(QStringLiteral("FieldLabel"));
        auto *val = new QLabel(QString::fromUtf8("—"), row);
        val->setObjectName(QStringLiteral("MonoValue"));
        top->addWidget(*label);
        top->addStretch(1);
        top->addWidget(val);
        *bar = new QProgressBar(row);
        (*bar)->setObjectName(QStringLiteral("DistBar"));
        (*bar)->setRange(0, 100);
        (*bar)->setValue(0);
        (*bar)->setTextVisible(false);
        rv->addLayout(top);
        rv->addWidget(*bar);
        cv->addWidget(row);
        return val;
    };
    makeCmpRow(&m_cmpCurLabel, &m_cmpCurBar);
    makeCmpRow(&m_cmpPrevLabel, &m_cmpPrevBar);
    v->addWidget(cmp);

    m_evalInfo = subLabel(QString(), page);
    m_evalInfo->setWordWrap(true);
    v->addWidget(m_evalInfo);
    v->addStretch(1);

    auto *foot = new QHBoxLayout();
    auto *refresh = new QPushButton(tr("刷新"), page);
    refresh->setObjectName(QStringLiteral("GhostBtn"));
    auto *publish = new QPushButton(tr("进入发布部署 →"), page);
    publish->setObjectName(QStringLiteral("PrimaryButton"));
    publish->setCursor(Qt::PointingHandCursor);
    foot->addWidget(refresh);
    foot->addStretch(1);
    foot->addWidget(publish);
    v->addLayout(foot);

    connect(refresh, &QPushButton::clicked, this, &TrainingToolWindow::refreshEval);
    connect(publish, &QPushButton::clicked, this, [this]() { showPage(5); });
    return page;
}

// ---------------------------------------------------------------------------
// Page 5: publish
// ---------------------------------------------------------------------------
QWidget *TrainingToolWindow::buildPublishPage()
{
    auto *page = new QWidget(m_stack);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);
    v->addWidget(pageTitle(tr("一键发布与部署"), page));

    auto *fmtCard = new QFrame(page);
    fmtCard->setObjectName(QStringLiteral("PanelCard"));
    auto *fv = new QVBoxLayout(fmtCard);
    fv->setContentsMargins(14, 12, 14, 12);
    fv->setSpacing(10);
    fv->addWidget(cardTitle(tr("导出格式"), fmtCard));
    auto *fmtRow = new QHBoxLayout();
    fmtRow->setSpacing(10);
    auto fmtCard2 = [fmtCard](const QString &name, const QString &sub, bool on) {
        auto *card = new QFrame(fmtCard);
        card->setObjectName(on ? QStringLiteral("FmtCardOn") : QStringLiteral("FmtCard"));
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(12, 8, 12, 8);
        cv->setSpacing(2);
        auto *a = new QLabel(name, card);
        a->setObjectName(QStringLiteral("FmtTitle"));
        auto *b = new QLabel(sub, card);
        b->setObjectName(QStringLiteral("FmtSub"));
        cv->addWidget(a);
        cv->addWidget(b);
        return card;
    };
    fmtRow->addWidget(fmtCard2(tr("ONNX (.onnx)"), tr("DirectML 推理 · 当前产物"), true));
    fmtRow->addWidget(fmtCard2(tr("TensorRT (.engine)"), tr("暂未支持"), false));
    fmtRow->addWidget(fmtCard2(tr("PyTorch (.pt)"), tr("暂未支持"), false));
    fv->addLayout(fmtRow);
    m_pubInfo = subLabel(QString(), fmtCard);
    m_pubInfo->setWordWrap(true);
    fv->addWidget(m_pubInfo);
    v->addWidget(fmtCard);

    auto *verCard = new QFrame(page);
    verCard->setObjectName(QStringLiteral("PanelCard"));
    auto *vv = new QVBoxLayout(verCard);
    vv->setContentsMargins(14, 12, 14, 12);
    vv->setSpacing(8);
    vv->addWidget(cardTitle(tr("本地模型版本"), verCard));
    m_verList = new QListWidget(verCard);
    m_verList->setObjectName(QStringLiteral("EvidenceList"));
    vv->addWidget(m_verList, 1);
    v->addWidget(verCard, 1);

    auto *row = new QHBoxLayout();
    auto *refresh = new QPushButton(tr("刷新"), page);
    refresh->setObjectName(QStringLiteral("GhostBtn"));
    auto *apply = new QPushButton(tr("推送至播放器"), page);
    apply->setObjectName(QStringLiteral("PrimaryButton"));
    apply->setCursor(Qt::PointingHandCursor);
    row->addWidget(refresh);
    row->addStretch(1);
    row->addWidget(apply);
    v->addLayout(row);

    connect(refresh, &QPushButton::clicked, this, &TrainingToolWindow::refreshPublish);
    connect(apply, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem *it = m_verList->currentItem()) {
            setCurrentVersion(it->data(Qt::UserRole).toString());
        }
    });
    return page;
}

// ---------------------------------------------------------------------------
// Navigation / data
// ---------------------------------------------------------------------------
void TrainingToolWindow::showPage(int index)
{
    if (index < 0 || index > 5) {
        return;
    }
    m_page = index;
    if (m_stack) {
        m_stack->setCurrentIndex(index);
    }
    if (index < m_navBtns.size() && m_navBtns[index]) {
        m_navBtns[index]->setChecked(true);
    }
    if (index == 1) {
        if (!m_records.isEmpty() && m_current < 0) {
            openRecord(0);
        } else if (m_current >= 0) {
            refreshInspector();
        }
    } else if (index == 2) {
        refreshQc();
    } else if (index == 4) {
        refreshEval();
    } else if (index == 5) {
        refreshPublish();
    }
}

void TrainingToolWindow::reload()
{
    m_records = DatasetStore::instance().load();
    reloadQueue();

    int boxes = 0;
    for (const SampleRecord &r : m_records) {
        boxes += r.boxes.size();
    }
    if (m_navQueueBadge) {
        m_navQueueBadge->setText(QString::number(m_records.size()));
        m_navQueueBadge->setVisible(!m_records.isEmpty());
    }
    if (m_navQcBadge) {
        m_navQcBadge->setText(tr("%1框").arg(boxes));
        m_navQcBadge->setVisible(boxes > 0);
    }
    emit sampleCountChanged(m_records.size());
}

void TrainingToolWindow::goToStage(int index)
{
    showPage(index);
}

void TrainingToolWindow::openRecordById(const QString &id)
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records[i].id == id) {
            openRecord(i);
            return;
        }
    }
    showPage(0);
}

void TrainingToolWindow::openRecord(int index)
{
    if (index < 0 || index >= m_records.size()) {
        return;
    }
    m_current = index;
    const SampleRecord &r = m_records[index];
    QImage img(r.framePath);
    m_canvas->setImage(img);
    // Normalise placeholder labels ("#N") or empty ones to the project class
    // name so the canvas chips read the same as the inspector.
    QVector<AnalysisBox> boxes = r.boxes;
    for (AnalysisBox &b : boxes) {
        const QString fallback = DatasetStore::instance().className(b.classId);
        if (!fallback.isEmpty()
            && (b.label.isEmpty() || b.label.startsWith(QLatin1Char('#')))) {
            b.label = fallback;
        }
    }
    m_canvas->setBoxes(boxes);
    m_annoName->setText(r.channel);
    uikit::setChip(m_annoReason,
                   r.reason.isEmpty() ? tr("manual") : r.reason,
                   r.reason == QLatin1String("low_conf") ? ChipWarn : ChipMuted);
    m_annoIndex->setText(QStringLiteral("%1 / %2").arg(index + 1).arg(m_records.size()));
    refreshInspector();
    showPage(1);
}

void TrainingToolWindow::saveCurrent()
{
    if (m_current < 0 || m_current >= m_records.size()) {
        return;
    }
    SampleRecord &r = m_records[m_current];
    r.boxes = m_canvas->boxes();
    DatasetStore::instance().saveLabels(r, r.boxes);
    r.done = true;
    DatasetStore::instance().rewrite(m_records);
    reload();
    if (m_current + 1 < m_records.size()) {
        openRecord(m_current + 1);
    } else {
        m_annoName->setText(tr("已保存 · 这是最后一张"));
        refreshInspector();
    }
}

// ---------------------------------------------------------------------------
// Quality check
// ---------------------------------------------------------------------------
void TrainingToolWindow::refreshQc()
{
    const QVector<SampleRecord> recs = DatasetStore::instance().load();
    const QVector<ClassDef> cls = DatasetStore::instance().classes();
    int done = 0, boxes = 0, unknown = 0;
    QHash<int, int> counts;   // class id -> box count
    for (const SampleRecord &r : recs) {
        if (r.done) {
            ++done;
        }
        for (const AnalysisBox &b : r.boxes) {
            ++boxes;
            if (DatasetStore::instance().classIndex(b.classId) >= 0) {
                ++counts[b.classId];
            } else {
                ++unknown;
            }
        }
    }
    const ThemeTokens tk = ThemeManager::instance().tokens();
    if (m_kpiTotal) {
        m_kpiTotal->setText(QString::number(recs.size()));
    }
    if (m_kpiDone) {
        m_kpiDone->setText(QString::number(done));
        m_kpiDone->setStyleSheet(QStringLiteral("color:%1;").arg(tk.success.name()));
    }
    if (m_kpiTodo) {
        m_kpiTodo->setText(QString::number(recs.size() - done));
        m_kpiTodo->setStyleSheet(QStringLiteral("color:%1;").arg(tk.muted.name()));
    }
    if (m_kpiBoxes) {
        m_kpiBoxes->setText(QString::number(boxes));
        m_kpiBoxes->setStyleSheet(QStringLiteral("color:%1;").arg(tk.accent.name()));
    }
    if (m_qcBadge) {
        if (recs.isEmpty()) {
            uikit::setChip(m_qcBadge, tr("暂无样本"), ChipMuted);
        } else if (unknown > 0) {
            uikit::setChip(m_qcBadge, tr("有 %1 个框类别无效").arg(unknown), ChipBad);
        } else if (done == recs.size()) {
            uikit::setChip(m_qcBadge, tr("✓ 质检通过"), ChipOk);
        } else {
            uikit::setChip(m_qcBadge, tr("待完善 %1/%2").arg(done).arg(recs.size()), ChipWarn);
        }
    }

    // Distribution: one row per project class (custom classes included).
    if (m_distHost) {
        while (QLayoutItem *it = m_distHost->takeAt(0)) {
            if (QWidget *w = it->widget()) {
                w->deleteLater();
            }
            delete it;
        }
        m_distBars.clear();
        m_distVals.clear();
        QWidget *host = m_distHost->parentWidget();
        const QColor palette[] = {tk.accent, tk.success, tk.warning,
                                  QColor(0x8B, 0x5C, 0xF6), QColor(0x0E, 0xA5, 0xE9),
                                  QColor(0xF9, 0x73, 0x16)};
        const int paletteN = int(sizeof(palette) / sizeof(palette[0]));
        for (int i = 0; i < cls.size(); ++i) {
            const ClassDef &c = cls[i];
            const int count = counts.value(c.id, 0);
            const int pct = boxes > 0 ? int(100.0 * count / boxes + 0.5) : 0;
            const QString display = c.zh.isEmpty() ? c.name : c.zh;

            auto *row = new QWidget(host);
            auto *rv = new QVBoxLayout(row);
            rv->setContentsMargins(0, 0, 0, 0);
            rv->setSpacing(4);
            auto *top = new QHBoxLayout();
            top->setSpacing(6);
            auto *n = fieldLabel(QStringLiteral("%1 (%2)").arg(display, c.name), row);
            auto *val = new QLabel(QStringLiteral("%1 (%2%)").arg(count).arg(pct), row);
            val->setObjectName(QStringLiteral("MonoValue"));
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            val->setStyleSheet(count == 0
                ? QStringLiteral("color:%1;").arg(tk.muted.name()) : QString());
            top->addWidget(n);
            top->addStretch(1);
            top->addWidget(val);
            auto *bar = new QProgressBar(row);
            bar->setObjectName(QStringLiteral("DistBar"));
            bar->setRange(0, 100);
            bar->setValue(pct);
            bar->setTextVisible(false);
            bar->setStyleSheet(QStringLiteral(
                "QProgressBar::chunk { background: %1; border-radius: 4px; }")
                                   .arg(palette[i % paletteN].name()));
            rv->addLayout(top);
            rv->addWidget(bar);
            m_distHost->addWidget(row);
            m_distBars.append(bar);
            m_distVals.append(val);
        }
    }

    if (m_navQcBadge) {
        m_navQcBadge->setText(tr("%1框").arg(boxes));
        m_navQcBadge->setVisible(boxes > 0);
    }
    if (m_qcPath) {
        m_qcPath->setText(m_lastExportDir.isEmpty()
                              ? tr("尚未导出训练集（导出后可交给训练命令使用）")
                              : tr("最近导出：%1").arg(m_lastExportDir));
    }
}

void TrainingToolWindow::exportDataset()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择导出目录"));
    if (dir.isEmpty()) {
        return;
    }
    QString err;
    if (DatasetStore::instance().exportYolo(dir, 0.2, &err)) {
        m_lastExportDir = dir;
    }
    if (m_qcPath) {
        m_qcPath->setText(err.isEmpty() ? tr("已导出：%1（images/labels/data.yaml）").arg(dir)
                                        : tr("导出失败：%1").arg(err));
    }
}

// ---------------------------------------------------------------------------
// Training (runs an external command; registers the produced ONNX)
// ---------------------------------------------------------------------------
void TrainingToolWindow::startTraining()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        return;
    }
    if (m_lastExportDir.isEmpty()) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString dir = QDir(DatasetStore::instance().root())
                                .filePath(QStringLiteral("exports/") + stamp);
        QString err;
        if (!DatasetStore::instance().exportYolo(dir, 0.2, &err)) {
            if (m_trainStatus) {
                m_trainStatus->setText(tr("导出失败：%1").arg(err));
            }
            return;
        }
        m_lastExportDir = dir;
    }

    const QString data = m_lastExportDir;
    const QString out = QDir(m_lastExportDir).filePath(QStringLiteral("best.onnx"));
    const QString metrics = QDir(m_lastExportDir).filePath(QStringLiteral("metrics.json"));
    QString cmd = m_trainCmd ? m_trainCmd->text() : QString();
    cmd.replace(QStringLiteral("{data}"), data);
    cmd.replace(QStringLiteral("{epochs}"), QString::number(m_epochs ? m_epochs->value() : 200));
    cmd.replace(QStringLiteral("{imgsz}"), m_imgsz ? m_imgsz->currentData().toString()
                                                   : QStringLiteral("960"));
    cmd.replace(QStringLiteral("{batch}"), QString::number(m_batch ? m_batch->value() : 16));
    cmd.replace(QStringLiteral("{device}"), m_device ? m_device->currentData().toString()
                                                     : QStringLiteral("cpu"));
    cmd.replace(QStringLiteral("{out}"), out);
    cmd.replace(QStringLiteral("{metrics}"), metrics);

    const QStringList parts = splitCommand(cmd);
    if (parts.isEmpty()) {
        return;
    }

    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::readyRead, this, [this]() {
            const QString chunk = QString::fromLocal8Bit(m_proc->readAll());
            if (m_trainLog) {
                m_trainLog->appendPlainText(chunk);
            }
            // Pull an "X/Y" progress fragment (tqdm / "Epoch 3/200") out of the
            // stream so the bar reflects real training progress.
            static const QRegularExpression re(QStringLiteral("(\\d+)\\s*/\\s*(\\d+)"));
            int best = -1;
            auto it = re.globalMatch(chunk);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                const int num = m.captured(1).toInt();
                const int den = m.captured(2).toInt();
                if (den > 0 && den <= 2000) {
                    best = qMax(best, qBound(0, num * 100 / den, 100));
                }
            }
            if (best >= 0 && m_trainProg && best > m_trainProg->value()) {
                m_trainProg->setValue(best);
                if (m_trainPct) {
                    m_trainPct->setText(QStringLiteral("%1%").arg(best));
                }
            }
        });
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus) { onTrainFinished(code); });
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (m_trainLog) {
                m_trainLog->appendPlainText(tr("[process error] %1").arg(int(e)));
            }
        });
    }

    if (m_trainLog) {
        m_trainLog->clear();
        m_trainLog->appendPlainText(QStringLiteral("$ ") + cmd);
    }
    if (m_trainProg) {
        m_trainProg->setValue(0);
    }
    if (m_trainPct) {
        m_trainPct->setText(QStringLiteral("0%"));
    }
    if (m_trainStatus) {
        m_trainStatus->setText(tr("训练中…"));
    }
    if (m_startBtn) {
        m_startBtn->setEnabled(false);
        m_startBtn->setText(tr("训练中…"));
    }
    setConsoleState(true);
    {
        QSettings st;
        st.setValue(QStringLiteral("ai/epochs"), m_epochs ? m_epochs->value() : 200);
        st.setValue(QStringLiteral("ai/imgsz"), m_imgsz ? m_imgsz->currentData().toString()
                                                        : QStringLiteral("960"));
        st.setValue(QStringLiteral("ai/batch"), m_batch ? m_batch->value() : 16);
        st.setValue(QStringLiteral("ai/device"), m_device ? m_device->currentData().toString()
                                                          : QStringLiteral("cpu"));
        st.setValue(QStringLiteral("ai/trainCmd"), m_trainCmd ? m_trainCmd->text() : QString());
    }
    m_proc->setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_proc->start(parts.first(), parts.mid(1));
}

void TrainingToolWindow::onTrainFinished(int exitCode)
{
    setConsoleState(false);
    if (m_startBtn) {
        m_startBtn->setEnabled(true);
        m_startBtn->setText(tr("开始训练"));
    }
    const QString out = QDir(m_lastExportDir).filePath(QStringLiteral("best.onnx"));
    const QString metrics = QDir(m_lastExportDir).filePath(QStringLiteral("metrics.json"));
    if (QFileInfo::exists(out)) {
        const QString name = ModelRegistry::instance().suggestVersionName();
        QString err;
        if (ModelRegistry::instance().addVersion(name, out, metrics, &err)) {
            if (m_trainStatus) {
                m_trainStatus->setText(tr("训练完成，已注册模型 %1").arg(name));
            }
            if (m_trainProg) {
                m_trainProg->setValue(100);
            }
            if (m_trainPct) {
                m_trainPct->setText(QStringLiteral("100%"));
            }
        } else if (m_trainStatus) {
            m_trainStatus->setText(tr("注册失败：%1").arg(err));
        }
    } else if (m_trainStatus) {
        m_trainStatus->setText(tr("训练结束 (exit %1)，未找到输出模型，检查命令/环境").arg(exitCode));
    }
    refreshPublish();
    refreshEval();
}

// ---------------------------------------------------------------------------
// Evaluation / publish
// ---------------------------------------------------------------------------
void TrainingToolWindow::refreshEval()
{
    if (!m_evalInfo) {
        return;
    }
    const QVector<ModelVersion> vs = ModelRegistry::instance().versions();
    if (vs.isEmpty()) {
        m_evalInfo->setText(tr("暂无模型版本。完成训练，或手动放入 "
                               "models/versions/<v>/model.onnx。"));
        return;
    }
    const QString cur = ModelRegistry::instance().currentVersion();
    const ModelVersion *v = nullptr;
    for (const ModelVersion &x : vs) {
        if (x.name == cur) {
            v = &x;
        }
    }
    if (!v) {
        v = &vs.last();
    }
    auto metric = [](const ModelVersion &mv, const QStringList &keys) -> double {
        for (const QString &k : keys) {
            if (mv.metrics.contains(k)) {
                return mv.metrics.value(k).toDouble();
            }
        }
        return -1.0;
    };
    const QStringList kMap50 = {QStringLiteral("metrics/mAP50(B)"), QStringLiteral("mAP50")};
    const QStringList kMap5095 = {QStringLiteral("metrics/mAP50-95(B)"), QStringLiteral("mAP50-95")};
    const QStringList kPrec = {QStringLiteral("metrics/precision(B)"), QStringLiteral("precision")};
    const QStringList kRec = {QStringLiteral("metrics/recall(B)"), QStringLiteral("recall")};

    const ThemeTokens tk = ThemeManager::instance().tokens();
    const double map50 = metric(*v, kMap50);
    const double map5095 = metric(*v, kMap5095);
    const double prec = metric(*v, kPrec);
    const double rec = metric(*v, kRec);
    auto pct = [](double x) {
        return x < 0 ? QString::fromUtf8("—")
                     : QStringLiteral("%1%").arg(QString::number(x * 100.0, 'f', 1));
    };
    if (m_evMap50) {
        m_evMap50->setText(pct(map50));
        m_evMap50->setStyleSheet(QStringLiteral("color:%1;").arg(tk.accent.name()));
    }
    if (m_evMap5095) {
        m_evMap5095->setText(pct(map5095));
    }
    if (m_evPrec) {
        m_evPrec->setText(pct(prec));
    }
    if (m_evRecall) {
        m_evRecall->setText(pct(rec));
    }

    // Compare with the previous version.
    double prevMap50 = -1.0;
    const ModelVersion *prev = nullptr;
    for (int i = 0; i < vs.size(); ++i) {
        if (&vs[i] == v) {
            if (i > 0) {
                prev = &vs[i - 1];
                prevMap50 = metric(vs[i - 1], kMap50);
            }
            break;
        }
    }
    if (m_evDelta) {
        if (map50 >= 0 && prevMap50 >= 0) {
            const double d = (map50 - prevMap50) * 100.0;
            m_evDelta->setText(tr("较上一版 mAP@0.5：%1%2%")
                                   .arg(d >= 0 ? QStringLiteral("↑ +") : QStringLiteral("↓ "))
                                   .arg(QString::number(d, 'f', 1)));
            m_evDelta->setStyleSheet(QStringLiteral("color:%1;")
                                         .arg((d >= 0 ? tk.success : tk.danger).name()));
        } else {
            m_evDelta->setText(QString());
        }
    }
    if (m_cmpCurLabel) {
        m_cmpCurLabel->setText(tr("当前版本 (%1)").arg(v->name));
    }
    if (m_cmpPrevLabel) {
        m_cmpPrevLabel->setText(prev ? tr("上一版本 (%1)").arg(prev->name) : tr("上一版本"));
    }
    if (m_cmpCurBar) {
        const int p = map50 >= 0 ? qBound(0, int(map50 * 100.0 + 0.5), 100) : 0;
        m_cmpCurBar->setValue(p);
        m_cmpCurBar->setStyleSheet(QStringLiteral(
            "QProgressBar::chunk { background: %1; border-radius: 4px; }")
                                       .arg(tk.accent.name()));
    }
    if (m_cmpPrevBar) {
        const int p = prevMap50 >= 0 ? qBound(0, int(prevMap50 * 100.0 + 0.5), 100) : 0;
        m_cmpPrevBar->setValue(p);
        m_cmpPrevBar->setStyleSheet(QStringLiteral(
            "QProgressBar::chunk { background: %1; border-radius: 4px; }")
                                        .arg(tk.borderStrong.name()));
    }

    QString text = tr("版本：%1").arg(v->name);
    if (v->hasMetrics) {
        text += tr(" · 门禁：关键类召回不下降、误检不上升，方可在“发布”页设为当前。");
    } else {
        text += tr(" · 无 metrics.json（训练脚本未输出指标）。");
    }
    m_evalInfo->setText(text);
}

void TrainingToolWindow::refreshPublish()
{
    if (!m_verList) {
        return;
    }
    m_verList->clear();
    const QVector<ModelVersion> vs = ModelRegistry::instance().versions();
    const QString cur = ModelRegistry::instance().currentVersion();
    for (const ModelVersion &v : vs) {
        QString text = v.name + (v.name == cur ? tr("   ★ 当前") : QString());
        if (v.hasMetrics) {
            auto metric = [&v](const QStringList &keys) -> double {
                for (const QString &k : keys) {
                    if (v.metrics.contains(k)) {
                        return v.metrics.value(k).toDouble();
                    }
                }
                return -1.0;
            };
            const double map50 = metric({QStringLiteral("metrics/mAP50(B)"),
                                         QStringLiteral("mAP50"), QStringLiteral("map50")});
            const double map5095 = metric({QStringLiteral("metrics/mAP50-95(B)"),
                                           QStringLiteral("mAP50-95"), QStringLiteral("map50_95")});
            if (map50 > 0.0) {
                text += tr("   · mAP50 %1").arg(map50, 0, 'f', 3);
            }
            if (map5095 > 0.0) {
                text += tr(" · mAP50-95 %1").arg(map5095, 0, 'f', 3);
            }
        }
        auto *it = new QListWidgetItem(text, m_verList);
        it->setData(Qt::UserRole, v.name);
        if (v.name == cur) {
            m_verList->setCurrentItem(it);
        }
    }
    if (m_pubInfo) {
        m_pubInfo->setText(vs.isEmpty()
            ? tr("暂无版本。完成一次训练后，导出的 ONNX 会自动注册到这里。")
            : tr("当前：%1 · 共 %2 个版本（推送后播放器下次加载模型生效）")
                  .arg(cur.isEmpty() ? tr("未设置") : cur).arg(vs.size()));
    }
}

void TrainingToolWindow::setCurrentVersion(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    if (ModelRegistry::instance().setCurrent(name)) {
        if (m_pubInfo) {
            m_pubInfo->setText(tr("已推送到播放器：%1").arg(name));
        }
        refreshPublish();
        emit modelVersionChanged(name);
    }
}

} // namespace zp
