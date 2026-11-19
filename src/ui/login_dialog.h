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

class IconField;

// Brand-split login dialog. Server URL is configured in a settings sheet (kept
// out of the main form), persisted to QSettings.
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

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
    void openServerSettings();
    void syncFromPrefs();
    void updateServerHint();

    AuthHandler m_handler;
    bool m_dragging = false;
    QPoint m_dragOffset;

    QString m_baseUrl;
    IconField *m_user = nullptr;
    IconField *m_pass = nullptr;
    QCheckBox *m_remember = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QLabel *m_error = nullptr;
    QLabel *m_serverHint = nullptr;
};

} // namespace zp

#endif // ZP_LOGIN_DIALOG_H
