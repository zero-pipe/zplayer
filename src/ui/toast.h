#ifndef ZP_TOAST_H
#define ZP_TOAST_H

#include <QObject>

QT_BEGIN_NAMESPACE
class QLabel;
class QStatusBar;
class QTimer;
QT_END_NAMESPACE

namespace zp {

// Transient status-bar feedback. Messages live in a label docked at the far
// right of the status bar (next to the persistent login/channel text), styled
// by kind and automatically cleared after a few seconds. No floating overlay,
// so feedback never covers the title bar or the video tiles.
class Toast : public QObject {
    Q_OBJECT
public:
    enum Kind { Info, Success, Error };

    // Creates the message label inside `bar`; `persistent` text is restored
    // whenever a transient message expires.
    explicit Toast(QStatusBar *bar, QObject *parent = nullptr);

    void showMessage(const QString &text, Kind kind = Info, int msecs = 3000);
    void setPersistentText(const QString &text);

private:
    void clearMessage();

    QStatusBar *m_bar = nullptr;
    QLabel *m_label = nullptr;
    QTimer *m_timer = nullptr;
    QString m_persistent;
};

} // namespace zp

#endif // ZP_TOAST_H
