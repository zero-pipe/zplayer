#include "ui/device_tree_view.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/pill_segment.h"

#include <QApplication>
#include <QCursor>
#include <QTimer>
#include <QFile>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTreeView>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QModelIndex>
#include <QColor>

namespace zp {

namespace {
constexpr int RoleId        = Qt::UserRole + 1;
constexpr int RoleType      = Qt::UserRole + 2;
constexpr int RolePtz       = Qt::UserRole + 3;
constexpr int RoleOnline    = Qt::UserRole + 4;
constexpr int RoleDev       = Qt::UserRole + 5;
constexpr int RoleCh        = Qt::UserRole + 6;
constexpr int RoleName      = Qt::UserRole + 7;
constexpr int RoleLoaded    = Qt::UserRole + 8;
constexpr int RoleLoading   = Qt::UserRole + 9;
constexpr int RoleRes       = Qt::UserRole + 10; // channel resourceId (primary key)
constexpr int RoleDevCode   = Qt::UserRole + 11; // device internal code (D...)
constexpr int RoleChCode    = Qt::UserRole + 12; // channel internal code (C...)
} // namespace

// QTreeView subclass that paints the branch indicator itself. Qt's
// stylesheet `::branch` images lose to the global theme.qss/Fusion style in
// several states, so we draw a crisp chevron directly: ▶ closed / ▼ open on
// folder rows only, nothing on leaf rows. Hovering the branch cell highlights
// it, and a click there expands/collapses just like the native arrow.
class DeviceTreeView::BranchTreeView : public QTreeView {
public:
    explicit BranchTreeView(QWidget *parent = nullptr) : QTreeView(parent) {}

protected:
    void drawBranches(QPainter *painter, const QRect &rect,
                      const QModelIndex &index) const override
    {
        if (!model() || !index.isValid() || !model()->hasChildren(index)) {
            return; // leaves: keep the indent empty, no arrow or connector.
        }

        const bool hovered = rect.contains(mapFromGlobal(QCursor::pos()));
        if (hovered) {
            painter->fillRect(rect, QColor(255, 255, 255, 16));
        }

        const int indent = indentation();
        const int depth = branchDepth(index);
        const QRect cell(rect.left() + indent * depth, rect.top(),
                         indent, rect.height());
        // Match the arrow colour to the row text: accent when selected.
        painter->setPen(QPen(selectionModel() && selectionModel()->isSelected(index)
                             ? QColor(0x60, 0xA5, 0xFA) : QColor(0x9A, 0xA7, 0xB6),
                             1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPointF p1, p2, p3;
        const qreal cx = cell.center().x();
        const qreal cy = cell.center().y();
        if (isExpanded(index)) { // ▼
            p1 = QPointF(cx - 3.5, cy - 1.5);
            p2 = QPointF(cx, cy + 2.0);
            p3 = QPointF(cx + 3.5, cy - 1.5);
        } else {                 // ▶
            p1 = QPointF(cx - 1.5, cy - 3.5);
            p2 = QPointF(cx + 2.0, cy);
            p3 = QPointF(cx - 1.5, cy + 3.5);
        }
        painter->drawLine(p1, p2);
        painter->drawLine(p2, p3);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid() && model() && model()->hasChildren(idx)
            && event->pos().x() < branchIndentRight(idx)) {
            // Toggle immediately; the platform style used by the base class
            // can miss clicks on a custom-painted branch cell.
            setExpanded(idx, !isExpanded(idx));
            return;
        }
        QTreeView::mousePressEvent(event);
    }

private:
    int branchDepth(QModelIndex index) const
    {
        int depth = 0;
        while (index.parent().isValid()) {
            ++depth;
            index = index.parent();
        }
        return depth;
    }

    int branchIndentRight(const QModelIndex &index) const
    {
        return (branchDepth(index) + 1) * indentation();
    }
};

DeviceTreeView::DeviceTreeView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LeftPanel"));

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    auto *header = new QLabel(tr("资源目录"), this);
    header->setObjectName(QStringLiteral("PanelHeader"));
    header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    headerRow->addWidget(header);
    headerRow->addStretch(1);
    m_header = new QLabel(this);
    m_header->setObjectName(QStringLiteral("TreeCount"));
    m_header->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_header->setText(tr("0 通道在线"));
    headerRow->addWidget(m_header);

    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("SearchBox"));
    m_filter->setPlaceholderText(tr("搜索设备 / 通道…"));
    m_filter->setClearButtonEnabled(true);

    // 二级目录维度：搜索框下方、树的第一行上方。
    m_groupPill = new PillSegment(this);
    connect(m_groupPill, &PillSegment::currentGroupChanged,
            this, &DeviceTreeView::currentGroupChanged);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("组织目录")});

    m_view = new BranchTreeView(this);
    m_view->setObjectName(QStringLiteral("DeviceTree"));
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setIconSize(QSize(18, 18));
    m_view->setIndentation(16);
    m_view->setAnimated(true);
    m_view->setExpandsOnDoubleClick(false);

    // The branch column is painted by BranchTreeView::drawBranches() so the
    // chevrons render reliably under Fusion + the global theme.qss. Disable
    // the stylesheet's row decoration fill only — our own item rules keep
    // hover/selection states consistent.
    m_view->setStyleSheet(QStringLiteral(
        "QTreeView#DeviceTree { show-decoration-selected: 0; }"));

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 10, 10, 10);
    vl->setSpacing(8);
    vl->addLayout(headerRow);
    vl->addWidget(m_filter);
    vl->addWidget(m_groupPill);
    vl->addWidget(m_view, 1);

    // Test hook: simulate a branch click after layout so expansion can be
    // verified headlessly. Remove before final build.
    if (!qEnvironmentVariableIsEmpty("ZP_TEST_BRANCH")) {
        {
            QFile probe(qEnvironmentVariable("ZP_TEST_BRANCH") + QStringLiteral(".started"));
            if (probe.open(QIODevice::WriteOnly | QIODevice::Text)) probe.write("constructed");
        }
        QTimer::singleShot(2200, this, [this]() {
            auto *model = m_view->model();
            if (!model || model->rowCount() < 1) return;
            auto idx = model->index(0, 0);
            QRect vr = m_view->visualRect(idx);
            QPoint click(vr.left() + 8, vr.center().y());
            QString diag = QString("row=%1,%2,%3,%4 click=%5,%6 expanded=%7 depth0ind=%8")
                .arg(vr.left()).arg(vr.top()).arg(vr.width()).arg(vr.height())
                .arg(click.x()).arg(click.y()).arg(m_view->isExpanded(idx))
                .arg(m_view->indentation());
            QFile f(qEnvironmentVariable("ZP_TEST_BRANCH"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(diag.toUtf8());
            }
            QMouseEvent press(QEvent::MouseButtonPress, click, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, click, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(m_view, &press);
            QApplication::sendEvent(m_view, &release);
        });
    }

    connect(m_filter, &QLineEdit::textChanged, this, &DeviceTreeView::applyFilter);
    connect(m_view, &QTreeView::clicked, this, &DeviceTreeView::onActivated);
    connect(m_view, &QTreeView::activated, this, &DeviceTreeView::onActivated);
    connect(m_view, &QTreeView::expanded, this, &DeviceTreeView::onExpanded);
}

void DeviceTreeView::setHeaderSuffix(const QString &text)
{
    if (m_header) {
        m_header->setText(text);
    }
}

int DeviceTreeView::channelCount(QStandardItem *item) const
{
    int n = 0;
    if (!item) {
        return 0;
    }
    if (item->data(RoleType).toString() == QLatin1String("channel")
        && item->data(RoleOnline).toBool()) {
        ++n;
    }
    for (int i = 0; i < item->rowCount(); ++i) {
        n += channelCount(item->child(i));
    }
    return n;
}

void DeviceTreeView::refreshHeaderCount()
{
    int n = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        n += channelCount(m_model->item(i));
    }
    setHeaderSuffix(tr("%1 通道在线").arg(n));
    // Keep the row aligned: Qt centres a QLabel vertically only when its
    // heightForWidth policy allows it to shrink to the font's line height.
    if (m_header) {
        m_header->adjustSize();
    }
}

void DeviceTreeView::setRootNodes(const QList<DeviceNode> &nodes)
{
    m_model->clear();
    m_model->setHorizontalHeaderLabels({tr("组织目录")});
    m_parentMap.clear();

    // Industry convention: always show a synthetic "根资源组" root so the
    // directory is never empty, matching the web platform's tree.
    auto *root = new QStandardItem(tr("根资源组"));
    root->setEditable(false);
    root->setData(QStringLiteral("root"), RoleId);
    root->setData(QStringLiteral("group"), RoleType);
    root->setData(true, RoleLoaded);
    root->setData(false, RoleLoading);
    root->setIcon(icons::folder(true));
    m_model->appendRow(root);

    if (nodes.isEmpty()) {
        auto *empty = new QStandardItem(tr("（暂无组织）"));
        empty->setEditable(false);
        empty->setForeground(QColor(0x5F, 0x6B, 0x7A));
        root->appendRow(empty);
    } else {
        for (const DeviceNode &n : nodes) {
            appendNode(root, n);
        }
    }
    m_view->setExpanded(root->index(), true);
    refreshHeaderCount();
}

void DeviceTreeView::appendNode(QStandardItem *parentItem, const DeviceNode &node)
{
    auto *item = new QStandardItem(node.name);
    item->setEditable(false);
    item->setData(node.id, RoleId);
    item->setData(node.type, RoleType);
    item->setData(node.ptzCapable, RolePtz);
    item->setData(node.online, RoleOnline);
    item->setData(node.ref.deviceId, RoleDev);
    item->setData(node.ref.channelId, RoleCh);
    item->setData(node.ref.resourceId, RoleRes);
    item->setData(node.ref.deviceCode, RoleDevCode);
    item->setData(node.ref.channelCode, RoleChCode);
    item->setData(node.name, RoleName);

    if (node.isGroup()) {
        item->setIcon(icons::folder(true));
        item->setData(false, RoleLoaded);
        item->setData(false, RoleLoading);
        m_parentMap.insert(node.id, item);
        // Placeholder so the expand arrow shows before loading.
        auto *placeholder = new QStandardItem(tr("加载中…"));
        placeholder->setEditable(false);
        placeholder->setForeground(QColor(0x5F, 0x6B, 0x7A));
        item->appendRow(placeholder);
    } else {
        item->setIcon(icons::camera(node.online));
        if (!node.online) {
            item->setForeground(QColor(0x6E, 0x7A, 0x88));
        }
        QString tip = node.name;
        if (!node.online) {
            tip += tr("  (离线)");
        }
        item->setToolTip(tip);
    }

    if (parentItem) {
        parentItem->appendRow(item);
    } else {
        m_model->appendRow(item);
    }
}

void DeviceTreeView::addChildren(const QString &parentId, const QList<DeviceNode> &nodes)
{
    QStandardItem *parentItem = m_parentMap.value(parentId, nullptr);
    if (!parentItem) {
        return;
    }
    parentItem->removeRows(0, parentItem->rowCount());
    parentItem->setData(true, RoleLoaded);
    parentItem->setData(false, RoleLoading);

    if (nodes.isEmpty()) {
        auto *empty = new QStandardItem(tr("（无设备）"));
        empty->setEditable(false);
        empty->setForeground(QColor(0x5F, 0x6B, 0x7A));
        parentItem->appendRow(empty);
    } else {
        for (const DeviceNode &n : nodes) {
            appendNode(parentItem, n);
        }
    }

    // Re-apply current filter to freshly loaded subtree.
    if (!m_filter->text().trimmed().isEmpty()) {
        applyFilter(m_filter->text());
    }
    refreshHeaderCount();
}

void DeviceTreeView::onExpanded(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item || item->data(RoleType).toString() != QLatin1String("group")) {
        return;
    }
    if (!item->data(RoleLoaded).toBool() && !item->data(RoleLoading).toBool()) {
        requestChildren(item);
    }
}

void DeviceTreeView::requestChildren(QStandardItem *item)
{
    item->setData(true, RoleLoading);
    const QString id = item->data(RoleId).toString();
    emit childrenNeeded(id);
}

void DeviceTreeView::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) {
        return;
    }
    const QString type = item->data(RoleType).toString();
    if (type == QLatin1String("channel")) {
        ChannelRef ref;
        ref.resourceId = item->data(RoleRes).toString();
        ref.deviceId = item->data(RoleDev).toString();
        ref.channelId = item->data(RoleCh).toString();
        ref.deviceCode = item->data(RoleDevCode).toString();
        ref.channelCode = item->data(RoleChCode).toString();
        emit channelActivated(ref, item->data(RoleName).toString(),
                              item->data(RolePtz).toBool());
    } else if (type == QLatin1String("group")) {
        if (!item->data(RoleLoaded).toBool() && !item->data(RoleLoading).toBool()) {
            requestChildren(item);
        }
        m_view->setExpanded(index, !m_view->isExpanded(index));
    }
}

void DeviceTreeView::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        filterItem(m_model->item(i), needle);
    }
}

bool DeviceTreeView::filterItem(QStandardItem *item, const QString &text)
{
    if (!item) {
        return false;
    }
    bool childVisible = false;
    for (int i = 0; i < item->rowCount(); ++i) {
        if (filterItem(item->child(i), text)) {
            childVisible = true;
        }
    }
    const bool selfMatch = text.isEmpty()
            || item->text().contains(text, Qt::CaseInsensitive);
    const bool visible = selfMatch || childVisible;
    QModelIndex idx = item->index();
    m_view->setRowHidden(idx.row(), idx.parent(), !visible);
    return visible;
}

} // namespace zp
