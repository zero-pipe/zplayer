#include "ui/device_tree_widget.h"

#include <QLineEdit>
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QModelIndex>

namespace zp {

namespace {
constexpr int RoleId = Qt::UserRole + 1;
constexpr int RoleType = Qt::UserRole + 2;
constexpr int RolePtz = Qt::UserRole + 3;
constexpr int RoleOnline = Qt::UserRole + 4;
} // namespace

DeviceTreeWidget::DeviceTreeWidget(QWidget *parent)
    : QWidget(parent)
{
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("搜索设备 / 通道…"));
    m_filter->setClearButtonEnabled(true);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("设备目录")});

    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(6);
    vl->addWidget(m_filter);
    vl->addWidget(m_view, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &DeviceTreeWidget::applyFilter);
    connect(m_view, &QTreeView::clicked, this, &DeviceTreeWidget::onItemClicked);
}

void DeviceTreeWidget::setDeviceTree(const QList<DeviceNode> &roots)
{
    m_model->clear();
    m_model->setHorizontalHeaderLabels({tr("设备目录")});
    for (const DeviceNode &node : roots) {
        appendNode(nullptr, node);
    }
    m_view->expandAll();
}

void DeviceTreeWidget::appendNode(QStandardItem *parent, const DeviceNode &node)
{
    auto *item = new QStandardItem(node.name);
    item->setEditable(false);
    item->setData(node.id, RoleId);
    item->setData(node.type, RoleType);
    item->setData(node.ptzCapable, RolePtz);
    item->setData(node.online, RoleOnline);

    if (!node.online) {
        item->setForeground(QColor("#9e9e9e"));
    }
    if (parent) {
        parent->appendRow(item);
    } else {
        m_model->appendRow(item);
    }

    for (const DeviceNode &child : node.children) {
        appendNode(item, child);
    }
}

void DeviceTreeWidget::onItemClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    if (m_model->rowCount(index) > 0) {
        return; // group node, not a playable channel
    }
    const QString id = index.data(RoleId).toString();
    const QString name = index.data(Qt::DisplayRole).toString();
    const bool ptz = index.data(RolePtz).toBool();
    emit deviceActivated(id, name, ptz);
}

void DeviceTreeWidget::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        filterItem(m_model->item(i), needle);
    }
}

bool DeviceTreeWidget::filterItem(QStandardItem *item, const QString &text)
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
    m_view->setRowHidden(item->row(), item->parent() ? item->parent()->index()
                                                      : QModelIndex(), !visible);
    return visible;
}

} // namespace zp
