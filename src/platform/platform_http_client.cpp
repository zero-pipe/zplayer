#include "platform/platform_http_client.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

namespace zp {

namespace {
constexpr int kTimeoutMs = 30000;
} // namespace

PlatformHttpClient::PlatformHttpClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

PlatformHttpClient::~PlatformHttpClient() = default;

QString PlatformHttpClient::normalizeBaseUrl(const QString &url)
{
    QString u = url.trimmed();
    if (u.isEmpty()) {
        return u;
    }
    if (!u.startsWith(QLatin1String("http://")) && !u.startsWith(QLatin1String("https://"))) {
        u.prepend(QLatin1String("http://"));
    }
    while (u.endsWith(QLatin1Char('/'))) {
        u.chop(1);
    }
    return u;
}

QString PlatformHttpClient::md5Hex(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

void PlatformHttpClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = normalizeBaseUrl(baseUrl);
}

QNetworkReply *PlatformHttpClient::get(const QString &path, const QUrlQuery &query)
{
    QUrl url(m_baseUrl + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_token.isEmpty()) {
        req.setRawHeader("access-token", m_token.toUtf8());
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    req.setTransferTimeout(kTimeoutMs);
#endif
    return m_nam->get(req);
}

void PlatformHttpClient::getJson(const QString &path, const QUrlQuery &query,
                                 const JsonCallback &cb)
{
    if (m_baseUrl.isEmpty()) {
        cb(false, QJsonValue(), tr("未配置服务器地址"), 0);
        return;
    }

    QNetworkReply *reply = get(path, query);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (status == 401) {
            emit unauthorized();
            if (cb) {
                cb(false, QJsonValue(), tr("登录已过期"), 401);
            }
            return;
        }

        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            if (cb) {
                cb(false, QJsonValue(), reply->errorString(), status);
            }
            return;
        }

        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            if (cb) {
                cb(false, QJsonValue(), tr("服务器响应无效"), status);
            }
            return;
        }

        const QJsonObject obj = doc.object();
        const int code = obj.value(QStringLiteral("code")).toInt(-1);
        const QString msg = obj.value(QStringLiteral("msg")).toString();
        const QJsonValue data = obj.value(QStringLiteral("data"));

        if (code == 0) {
            if (cb) {
                cb(true, data, msg, status);
            }
        } else {
            if (cb) {
                cb(false, data, msg.isEmpty() ? tr("请求失败(code=%1)").arg(code) : msg, status);
            }
        }
    });
}

} // namespace zp
