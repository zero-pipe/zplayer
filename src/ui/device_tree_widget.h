#ifndef ZP_DEVICE_TREE_WIDGET_H
#define ZP_DEVICE_TREE_WIDGET_H

#include <QWidget>
#include <QList>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
QT_END_NAMESPACE

namespace zp {

// Left panel: device tree pulled from the platform.
class DeviceTreeWidget : public QWidget {
    Q_OBJECT
public:
    explicit DeviceTreeWidget(QWidget *parent = nullptr);

    void setDeviceTree(const QList<DeviceNode> &roots);

signals:
    void deviceActivated(const QString &deviceId, const QString &name, bool ptzCapable);

private:
    void appendNode(QStandardItem *parent, const DeviceNode &node);
    void onItemClicked(const QModelIndex &index);
    void applyFilter(const QString &text);
    bool filterItem(QStandardItem *item, const QString &text);

    QLineEdit *m_filter = nullptr;
    QTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

} // namespace zp

#endif // ZP_DEVICE_TREE_WIDGET_H
