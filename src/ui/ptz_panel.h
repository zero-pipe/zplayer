#ifndef ZP_PTZ_PANEL_H
#define ZP_PTZ_PANEL_H

#include <QWidget>
#include <QList>

#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QSlider;
class QLabel;
class QToolButton;
class QListWidget;
class QStackedWidget;
class QPixmap;
QT_END_NAMESPACE

namespace zp {

// Right panel: directional pad + zoom + speed, with preset/cruise/guard tabs.
// Commands go through the platform (pass-through). Presets are backed by the
// platform API; cruise/guard are placeholders until the platform exposes them.
class PtzPanel : public QWidget {
    Q_OBJECT
public:
    explicit PtzPanel(QWidget *parent = nullptr);

    void setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable);
    void setPresets(const QList<PresetInfo> &presets);

signals:
    void commandIssued(const zp::ChannelRef &ch, const zp::PtzCommand &cmd);
    void presetQuery(const zp::ChannelRef &ch);
    void presetCall(const zp::ChannelRef &ch, int presetId);
    void presetAdd(const zp::ChannelRef &ch, int presetId);
    void presetDelete(const zp::ChannelRef &ch, int presetId);

private:
    QToolButton *makeDirButton(QWidget *parent, const QPixmap &icon,
                               PtzCommand::Type type, const QString &tip);
    void updateEnabled();
    void refreshPresets();
    void emitDir(PtzCommand::Type type);
    int nextPresetId() const;

    ChannelRef m_channel;
    QString m_name;
    bool m_ptzEnabled = false;
    int m_baseSpeed = 5;
    QList<PresetInfo> m_presets;

    QLabel *m_target = nullptr;
    QLabel *m_notice = nullptr;   // inline capability note (no toast needed)
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedValue = nullptr;
    QStackedWidget *m_tabs = nullptr;
    QListWidget *m_presetList = nullptr;
    QWidget *m_dpadHost = nullptr;
    QWidget *m_zoomHost = nullptr;
    QWidget *m_speedHost = nullptr;
};

} // namespace zp

#endif // ZP_PTZ_PANEL_H
