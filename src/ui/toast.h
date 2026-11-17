#ifndef ZP_TOAST_H
#define ZP_TOAST_H

#include <QFrame>

QT_BEGIN_NAMESPACE
class QLabel;
class QTimer;
QT_END_NAMESPACE

namespace zp {

// Lightweight floating notification anchored to the bottom-right of a parent.
class Toast : public QFrame {
    Q_OBJECT
public:
    enum Kind { Info, Success, Error };

    explicit Toast(QWidget *parent);

    void showMessage(const QString &text, Kind kind = Info, int msecs = 3000);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();

    QLabel *m_text = nullptr;
    QTimer *m_timer = nullptr;
};

} // namespace zp

#endif // ZP_TOAST_H
