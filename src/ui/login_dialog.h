#ifndef ZP_LOGIN_DIALOG_H
#define ZP_LOGIN_DIALOG_H

#include <QDialog>
#include <functional>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
QT_END_NAMESPACE

namespace zp {

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    // The app injects how to perform authentication. The handler receives the
    // submitted credentials; on success it should call accept() results via the
    // dialog's callbacks. Because login is async, this is invoked on click.
    using AuthHandler = std::function<void(const QString &baseUrl, const QString &user,
                                           const QString &password, bool offline)>;
    void setAuthHandler(AuthHandler handler);

    QString baseUrl() const;
    QString username() const;
    QString password() const;
    bool offlineDemo() const;
    bool rememberPassword() const;

    void setBusy(bool busy);
    void showError(const QString &message);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    void submit();
    void buildUi();

    AuthHandler m_handler;
    bool m_dragging = false;
    QPoint m_dragOffset;

    QLineEdit *m_server = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_pass = nullptr;
    QCheckBox *m_remember = nullptr;
    QCheckBox *m_offline = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace zp

#endif // ZP_LOGIN_DIALOG_H
