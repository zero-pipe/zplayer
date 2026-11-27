#include "ui/device_tree_view.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QLineEdit>
#include <QTreeView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
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
} // namespace

DeviceTreeView::DeviceTreeView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LeftPanel"));

    auto *header = new QLabel(tr("组织目录"), this);
    header->setObjectName(QStringLiteral("PanelHeader"));

    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("SearchBox"));
    m_filter->setPlaceholderText(tr("搜索设备 / 通道…"));
    m_filter->setClearButtonEnabled(true);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("组织目录")});

    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("DeviceTree"));
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setIconSize(QSize(18, 18));
    m_view->setIndentation(14);
    m_view->setAnimated(true);
    m_view->setExpandsOnDoubleClick(false);

    // Take over the branch (indent) column completely. Without explicit rules
    // for :selected / :hover, Qt falls back to the style's primitive and paints
    // the whole cell with the highlight colour — the solid blue block seen when
    // a directory row is clicked. Every state is listed so the fallback never
    // happens, and the arrows are our own chevrons so they match the theme.
    // show-decoration-selected:0 is the key switch: it disables the
    // PE_PanelItemViewRow fill that paints palette.highlight() behind the
    // branch cell on selection (see qstylesheetstyle.cpp drawControl).
    m_view->setStyleSheet(QStringLiteral(
        "QTreeView#DeviceTree { show-decoration-selected: 0; }"
        "QTreeView#DeviceTree::branch {"
        "  background: transparent; border: none;"
        "}"
        "QTreeView#DeviceTree::branch:hover,"
        "QTreeView#DeviceTree::branch:selected,"
        "QTreeView#DeviceTree::branch:selected:hover {"
        "  background: transparent; border: none; image: none;"
        "}"
        "QTreeView#DeviceTree::branch:has-siblings:!adjoins-item,"
        "QTreeView#DeviceTree::branch:has-siblings:adjoins-item,"
        "QTreeView#DeviceTree::branch:!has-children:!has-siblings:adjoins-item {"
        "  background: transparent; border: none; image: none;"
        "}"
        "QTreeView#DeviceTree::branch:closed:has-children,"
        "QTreeView#DeviceTree::branch:closed:has-children:has-siblings,"
        "QTreeView#DeviceTree::branch:closed:has-children:!has-siblings {"
        "  background: transparent; image: url(:/resources/branch-closed.svg);"
        "}"
        "QTreeView#DeviceTree::branch:open:has-children,"
        "QTreeView#DeviceTree::branch:open:has-children:has-siblings,"
        "QTreeView#DeviceTree::branch:open:has-children:!has-siblings {"
        "  background: transparent; image: url(:/resources/branch-open.svg);"
        "}"
        // Last, so it wins for the pressed state regardless of the row's
        // selection/hover state (the platform style would otherwise paint its
        // highlight colour into the cell while the mouse button is down).
        "QTreeView#DeviceTree::branch:pressed {"
        "  background: transparent; border: none;"
        "}"));

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 14, 10, 12);
    vl->setSpacing(8);
    vl->addWidget(header);
    vl->addWidget(m_filter);
    vl->addWidget(m_view, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &DeviceTreeView::applyFilter);
    connect(m_view, &QTreeView::clicked, this, &DeviceTreeView::onActivated);
    connect(m_view, &QTreeView::activated, this, &DeviceTreeView::onActivated);
    connect(m_view, &QTreeView::expanded, this, &DeviceTreeView::onExpanded);
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
