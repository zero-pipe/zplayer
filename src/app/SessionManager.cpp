#include "app/SessionManager.h"

#include <QByteArray>
#include <QSettings>

namespace zp {

namespace {
const char *kBaseUrl   = "server/baseUrl";
const char *kUsername  = "auth/username";
const char *kPassword  = "auth/passwordEnc";
const char *kRemember  = "auth/remember";
const char *kOffline   = "app/offlineDemo";
const char *kLayout    = "ui/layout";
const char *kKey       = "zP-session-2026";
} // namespace

SessionManager &SessionManager::instance()
{
    static SessionManager s;
    return s;
}

QString SessionManager::encode(const QString &plain)
{
    if (plain.isEmpty()) {
        return QString();
    }
    const QByteArray key(kKey);
    QByteArray raw = plain.toUtf8();
    for (int i = 0; i < raw.size(); ++i) {
        raw[i] = raw[i] ^ key.at(i % key.size());
    }
    return QString::fromLatin1(raw.toBase64());
}

QString SessionManager::decode(const QString &enc)
{
    if (enc.isEmpty()) {
        return QString();
    }
    const QByteArray key(kKey);
    QByteArray raw = QByteArray::fromBase64(enc.toLatin1());
    for (int i = 0; i < raw.size(); ++i) {
        raw[i] = raw[i] ^ key.at(i % key.size());
    }
    return QString::fromUtf8(raw);
}

SessionManager::LoginPrefs SessionManager::loadLoginPrefs() const
{
    QSettings s;
    LoginPrefs p;
    p.baseUrl = s.value(kBaseUrl, QStringLiteral("http://127.0.0.1:18080")).toString();
    p.username = s.value(kUsername).toString();
    p.remember = s.value(kRemember, false).toBool();
    p.offlineDemo = s.value(kOffline, false).toBool();
    if (p.remember) {
        p.password = decode(s.value(kPassword).toString());
    }
    return p;
}

void SessionManager::rememberOnSuccess(const QString &baseUrl, const QString &username,
                                       const QString &password, bool remember)
{
    QSettings s;
    s.setValue(kBaseUrl, baseUrl);
    s.setValue(kUsername, username);
    s.setValue(kRemember, remember);
    if (remember) {
        s.setValue(kPassword, encode(password));
    } else {
        s.remove(kPassword);
    }
    s.sync();
}

void SessionManager::saveLoginPrefs(bool rememberPassword)
{
    QSettings s;
    s.setValue(kRemember, rememberPassword);
    if (!rememberPassword) {
        s.remove(kPassword);
    }
    s.sync();
}

void SessionManager::clearRememberedPassword()
{
    QSettings s;
    s.remove(kPassword);
    s.setValue(kRemember, false);
    s.sync();
}

int SessionManager::lastLayout() const
{
    QSettings s;
    return s.value(kLayout, 1).toInt();
}

void SessionManager::setLastLayout(int index)
{
    QSettings s;
    s.setValue(kLayout, index);
}

QString SessionManager::theme() const
{
    QSettings s;
    return s.value("ui/theme", QStringLiteral("light")).toString();
}

void SessionManager::setTheme(const QString &name)
{
    QSettings s;
    s.setValue("ui/theme", name);
}

QString SessionManager::serverAlias() const
{
    QSettings s;
    return s.value("server/alias").toString();
}

void SessionManager::setServerAlias(const QString &alias)
{
    QSettings s;
    s.setValue("server/alias", alias);
}

void SessionManager::setBaseUrl(const QString &baseUrl)
{
    QSettings s;
    s.setValue(kBaseUrl, baseUrl);
}

} // namespace zp
