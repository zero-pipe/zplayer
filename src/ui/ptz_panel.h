#ifndef ZP_PTZ_PANEL_H
#define ZP_PTZ_PANEL_H

#include <QWidget>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QToolButton;
class QSlider;
class QLabel;
class QListWidget;
QT_END_NAMESPACE

namespace zp {

class Toast;

// Right panel: PTZ direction/zoom + speed + presets. Commands go through the
// platform (pass-through), never directly to the device.
class PtzPanel : public QWidget {
    Q_OBJECT
public:
    explicit PtzPanel(QWidget *parent = nullptr);

    void setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable);
    void setPresets(const QList<PresetInfo> &presets);

signals:
    void commandIssued(const zp::ChannelRef &ch, const zp::PtzCommand &cmd);
    void queryPresetsRequested(const zp::ChannelRef &ch);
    void callPresetRequested(const zp::ChannelRef &ch, int presetId);
    void addPresetRequested(const zp::ChannelRef &ch, int presetId);
    void deletePresetRequested(const zp::ChannelRef &ch, int presetId);

private:
    void send(PtzCommand::Type type);
    QToolButton *makeDirButton(const QString &glyph, const QString &tip, PtzCommand::Type type);
    void updateEnabled();
    void refreshPresets();

    ChannelRef m_channel;
    QString m_name;
    bool m_ptzEnabled = false;
    int m_speed = 5;

    QLabel *m_target = nullptr;
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedValue = nullptr;
    QList<QToolButton *> m_dirButtons;
    QListWidget *m_presetList = nullptr;
    QWidget *m_ptzSection = nullptr;
};

} // namespace zp

#endif // ZP_PTZ_PANEL_H
