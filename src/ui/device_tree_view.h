#ifndef ZP_DEVICE_TREE_VIEW_H
#define ZP_DEVICE_TREE_VIEW_H

#include <QWidget>
#include <QHash>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
QT_END_NAMESPACE

namespace zp {

// Left panel: organization directory, lazily loaded from the platform.
// Groups request their children when expanded (childrenNeeded); channels are
// leaf items that emit channelActivated when clicked.
class DeviceTreeView : public QWidget {
    Q_OBJECT
public:
    explicit DeviceTreeView(QWidget *parent = nullptr);

    void setRootNodes(const QList<DeviceNode> &nodes);
    void addChildren(const QString &parentId, const QList<DeviceNode> &nodes);

signals:
    void channelActivated(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void childrenNeeded(const QString &parentId);

private:
    void appendNode(QStandardItem *parentItem, const DeviceNode &node);
    void onExpanded(const QModelIndex &index);
    void onActivated(const QModelIndex &index);
    void requestChildren(QStandardItem *item);
    void applyFilter(const QString &text);
    bool filterItem(QStandardItem *item, const QString &text);
    static int roleLoaded() { return Qt::UserRole + 8; }

    QLineEdit *m_filter = nullptr;
    QTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QHash<QString, QStandardItem *> m_parentMap;
};

} // namespace zp

#endif // ZP_DEVICE_TREE_VIEW_H
