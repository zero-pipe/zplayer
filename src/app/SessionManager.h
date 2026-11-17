#ifndef ZP_SESSION_MANAGER_H
#define ZP_SESSION_MANAGER_H

#include <QString>

namespace zp {

// Persists connection preferences (server URL, username, remembered password,
// offline-demo flag, layout) via QSettings. The password is stored only when
// "remember" is on, using a lightweight reversible obfuscation (NOT encryption)
// purely for local convenience — never as a security boundary.
class SessionManager {
public:
    struct LoginPrefs {
        QString baseUrl;
        QString username;
        QString password;   // decoded plaintext (may be empty)
        bool remember = false;
        bool offlineDemo = false;
    };

    static SessionManager &instance();

    LoginPrefs loadLoginPrefs() const;
    void saveLoginPrefs(bool rememberPassword);

    // Call when a login succeeds: stores server/username and (optionally) pwd.
    void rememberOnSuccess(const QString &baseUrl, const QString &username,
                           const QString &password, bool remember);

    void clearRememberedPassword();

    int lastLayout() const;
    void setLastLayout(int index);

private:
    SessionManager() = default;
    static QString encode(const QString &plain);
    static QString decode(const QString &enc);
};

} // namespace zp

#endif // ZP_SESSION_MANAGER_H
