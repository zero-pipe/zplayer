#ifndef ZP_PLATFORM_HTTP_CLIENT_H
#define ZP_PLATFORM_HTTP_CLIENT_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonObject>
#include <QJsonValue>

#include <functional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace zp {

// Thin wrapper over QNetworkAccessManager for the zero-web-server REST API:
//   - normalizes the base URL
//   - injects the "access-token" header
//   - parses the {code,msg,data} envelope
//   - surfaces HTTP 401 as unauthorized()
//
// All calls are async; results come back through the reply callback on the
// Qt event loop (main thread).
class PlatformHttpClient : public QObject {
    Q_OBJECT
public:
    using JsonCallback = std::function<void(bool ok, const QJsonValue &data,
                                            const QString &msg, int httpStatus)>;

    explicit PlatformHttpClient(QObject *parent = nullptr);
    ~PlatformHttpClient() override;

    void setBaseUrl(const QString &baseUrl);
    QString baseUrl() const { return m_baseUrl; }

    void setToken(const QString &token) { m_token = token; }
    QString token() const { return m_token; }
    void clearToken() { m_token.clear(); }

    // GET the given path (relative to baseUrl) with optional query.
    void getJson(const QString &path, const QUrlQuery &query, const JsonCallback &cb);

    // Convenience overloads.
    void getJson(const QString &path, const JsonCallback &cb) {
        getJson(path, QUrlQuery(), cb);
    }

    // POST the given path with optional query and a JSON object body.
    // Used for endpoints like live/start|stop that are POST with an empty body.
    void postJson(const QString &path, const QUrlQuery &query,
                  const QJsonObject &body, const JsonCallback &cb);
    void postJson(const QString &path, const QJsonObject &body, const JsonCallback &cb) {
        postJson(path, QUrlQuery(), body, cb);
    }

    static QString normalizeBaseUrl(const QString &url);
    static QString md5Hex(const QString &text);

signals:
    void unauthorized();

private:
    QNetworkReply *get(const QString &path, const QUrlQuery &query);
    QNetworkReply *post(const QString &path, const QUrlQuery &query,
                        const QJsonObject &body);
    void handleReply(QNetworkReply *reply, const JsonCallback &cb);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_token;
};

} // namespace zp

#endif // ZP_PLATFORM_HTTP_CLIENT_H
