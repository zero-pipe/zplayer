#ifndef ZP_VIDEO_SETTINGS_DIALOG_H
#define ZP_VIDEO_SETTINGS_DIALOG_H

#include <QDialog>

#include "player/media_player.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QSlider;
QT_END_NAMESPACE

namespace zp {

// Non-modal-safe settings sheet for the engine-neutral VideoSettings: hardware
// decoding, picture adjustments, and subtitles. Every control emits
// settingsChanged() live so the caller can push it straight to the grid/engine;
// there is no OK/Apply gate.
class VideoSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit VideoSettingsDialog(QWidget *parent = nullptr);

    void setSettings(const VideoSettings &s);
    VideoSettings settings() const { return m_settings; }

signals:
    void settingsChanged(const zp::VideoSettings &settings);

private:
    void emitChanged();

    VideoSettings m_settings;

    QComboBox *m_hwdec = nullptr;
    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_hue = nullptr;
    QLabel *m_brightnessVal = nullptr;
    QLabel *m_contrastVal = nullptr;
    QLabel *m_saturationVal = nullptr;
    QLabel *m_hueVal = nullptr;
    QCheckBox *m_deinterlace = nullptr;

    QCheckBox *m_subEnabled = nullptr;
    QCheckBox *m_subAutoLoad = nullptr;
    QLineEdit *m_subFile = nullptr;
};

} // namespace zp

#endif // ZP_VIDEO_SETTINGS_DIALOG_H
