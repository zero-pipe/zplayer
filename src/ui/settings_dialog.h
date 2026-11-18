#ifndef ZP_SETTINGS_DIALOG_H
#define ZP_SETTINGS_DIALOG_H

#include <QDialog>

#include "player/media_player.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QListWidget;
class QStackedWidget;
QT_END_NAMESPACE

namespace zp {

class VideoSettingsPage;

// Modal settings window with a category list on the left and the selected
// category's detail on the right. Categories today: 视频 / 外观 / 关于.
// Changes apply live (signal out); the dialog itself holds no app state.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const VideoSettings &video, const QString &themeName,
                   QWidget *parent = nullptr);

    // Select a category by index (0 = 视频, 1 = 外观, 2 = 关于).
    void setPage(int index);

signals:
    void videoSettingsChanged(const zp::VideoSettings &settings);
    void themeChanged(const QString &name);

private:
    QWidget *buildAppearancePage(const QString &themeName);
    QWidget *buildAboutPage();

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    VideoSettingsPage *m_video = nullptr;
    QComboBox *m_theme = nullptr;
};

} // namespace zp

#endif // ZP_SETTINGS_DIALOG_H
