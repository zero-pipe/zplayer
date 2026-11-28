#ifndef ZP_DEVICE_TREE_VIEW_H
#define ZP_DEVICE_TREE_VIEW_H

#include <QWidget>
#include <QHash>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QLabel;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
QT_END_NAMESPACE

namespace zp {

class PillSegment;

// Left panel: organization directory, lazily loaded from the platform.
// Groups request their children when expanded (childrenNeeded); channels are
// leaf items that emit channelActivated when clicked.
class DeviceTreeView : public QWidget {
    Q_OBJECT
public:
    explicit DeviceTreeView(QWidget *parent = nullptr);

    void setRootNodes(const QList<DeviceNode> &nodes);
    void addChildren(const QString &parentId, const QList<DeviceNode> &nodes);

    // 资源目录标题后缀：显示在线通道数（设计稿右上的 "N 通道在线"）。
    void setHeaderSuffix(const QString &text);

signals:
    void channelActivated(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void childrenNeeded(const QString &parentId);
    // 目录维度变化（0 行政区划 / 1 业务分组），由内嵌药丸滑块发出。
    void currentGroupChanged(int index);

private:
    void appendNode(QStandardItem *parentItem, const DeviceNode &node);
    void onExpanded(const QModelIndex &index);
    void onActivated(const QModelIndex &index);
    void requestChildren(QStandardItem *item);
    void applyFilter(const QString &text);
    bool filterItem(QStandardItem *item, const QString &text);
    int channelCount(QStandardItem *item) const;
    void refreshHeaderCount();
    static int roleLoaded() { return Qt::UserRole + 8; }

    QLineEdit *m_filter = nullptr;
    QLabel *m_header = nullptr;
    PillSegment *m_groupPill = nullptr;   // 搜索框与树之间的二级维度滑块
    QTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QHash<QString, QStandardItem *> m_parentMap;
};

} // namespace zp

#endif // ZP_DEVICE_TREE_VIEW_H
