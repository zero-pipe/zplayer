#include "platform/web_platform_api.h"
#include "platform/platform_http_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>
#include <QUrlQuery>

namespace zp {

namespace {

QString pickPlayUrl(const QJsonObject &o, QString *transport)
{
    struct Cand { const char *key; const char *label; };
    static const Cand cands[] = {
        {"rtsp", "RTSP"},
        {"rtmp", "RTMP"},
        {"flv", "HTTP-FLV"},
        {"hls", "HLS"},
    };
    for (const Cand &c : cands) {
        const QString v = o.value(QLatin1String(c.key)).toString();
        if (!v.isEmpty()) {
            if (transport) {
                *transport = QLatin1String(c.label);
            }
            return v;
        }
    }
    if (transport) {
        transport->clear();
    }
    return QString();
}

} // namespace

WebPlatformApi::WebPlatformApi(QObject *parent)
    : IPlatformApi(parent)
    , m_http(new PlatformHttpClient(this))
{
    // A 401 from any call means the token expired -> tell the app to re-login.
    connect(m_http, &PlatformHttpClient::unauthorized,
            this, &IPlatformApi::sessionExpired);
}

QString WebPlatformApi::token() const
{
    return m_http->token();
}

void WebPlatformApi::login(const QString &baseUrl, const QString &username,
                           const QString &password)
{
    m_http->setBaseUrl(baseUrl);
    m_http->clearToken();
    m_username = username;

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("username"), username);
    q.addQueryItem(QStringLiteral("password"),
                   PlatformHttpClient::md5Hex(password));

    m_http->getJson(QStringLiteral("/api/v1/user/login"), q,
        [this](bool ok, const QJsonValue &data, const QString &msg, int) {
            if (!ok) {
                emit loginFailed(msg.isEmpty() ? tr("登录失败") : msg);
                return;
            }
            const QJsonObject o = data.toObject();
            const QString token = o.value(QStringLiteral("accessToken")).toString();
            if (token.isEmpty()) {
                emit loginFailed(tr("登录响应缺少令牌"));
                return;
            }
            m_http->setToken(token);
            m_username = o.value(QStringLiteral("username")).toString(m_username);
            m_serverId = o.value(QStringLiteral("serverId")).toString();

            Session s;
            s.username = m_username;
            s.serverId = m_serverId;
            s.valid = true;
            emit loginSucceeded(s);
        });
}

void WebPlatformApi::logout()
{
    if (!m_http->token().isEmpty()) {
        m_http->getJson(QStringLiteral("/api/v1/user/logout"),
                        [](bool, const QJsonValue &, const QString &, int) {});
    }
    m_http->clearToken();
}

void WebPlatformApi::fetchChildren(const QString &parentId)
{
    if (m_http->token().isEmpty()) {
        emit errorOccurred(QStringLiteral("catalog"), tr("尚未登录"));
        return;
    }

    QUrlQuery q;
    if (!parentId.isEmpty()) {
        q.addQueryItem(QStringLiteral("parent"), parentId);
        q.addQueryItem(QStringLiteral("hasChannel"), QStringLiteral("true"));
    }

    m_http->getJson(QStringLiteral("/api/v1/group/tree/list"), q,
        [this, parentId](bool ok, const QJsonValue &data, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("catalog"), msg);
                emit childrenReady(parentId, {}); // unblock UI in any case
                return;
            }
            // Channel leaves now carry their own resourceId in "id"; there is
            // no secondary device lookup to patch (that endpoint was removed).
            emit childrenReady(parentId, parseNodes(data));
        });
}

QList<DeviceNode> WebPlatformApi::parseNodes(const QJsonValue &data)
{
    QList<DeviceNode> out;
    if (!data.isArray()) {
        return out;
    }
    for (const QJsonValue &v : data.toArray()) {
        const QJsonObject o = v.toObject();
        const int type = o.value(QStringLiteral("type")).toInt(0);
        const QString nodeDeviceId = o.value(QStringLiteral("deviceId")).toString();
        const QString parentDeviceId = o.value(QStringLiteral("parentDeviceId")).toString();
        const QString status = o.value(QStringLiteral("status")).toString();
        const QString treeId = o.value(QStringLiteral("treeId")).toString();

        DeviceNode n;
        n.name = o.value(QStringLiteral("name")).toString();
        n.online = (status.compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0);

        if (type == 1) { // channel leaf
            n.type = QStringLiteral("channel");
            // resourceId = the directory tree leaf "id" (numeric). This is the
            // single key used to build every control URL downstream.
            n.ref.resourceId = o.value(QStringLiteral("id")).toVariant().toString();
            n.id = treeId.isEmpty()
                    ? QStringLiteral("ch%1").arg(n.ref.resourceId)
                    : treeId;
            // GB ids + internal codes retained for display / compatibility.
            n.ref.deviceId = parentDeviceId;
            n.ref.channelId = nodeDeviceId;
            n.ref.deviceCode = o.value(QStringLiteral("deviceInternalCode")).toString();
            n.ref.channelCode = o.value(QStringLiteral("internalCode")).toString();
            // GB channels are treated as PTZ-capable; the device ignores
            // unsupported commands. (ptzType isn't in the tree payload.)
            n.ptzCapable = true;
        } else { // group
            n.type = QStringLiteral("group");
            n.id = o.value(QStringLiteral("id")).toVariant().toString();
            n.online = true;
            n.ptzCapable = false;
        }
        out.append(n);
    }
    return out;
}

void WebPlatformApi::startPlay(const ChannelRef &ch)
{
    if (!ch.isValid()) {
        emit streamFailed(ch, tr("通道信息不完整"));
        return;
    }
    // POST /api/v1/resources/{id}/live/start  body {"profileId":""}
    const QString path = QStringLiteral("/api/v1/resources/%1/live/start")
            .arg(ch.resourceId);

    m_http->postJson(path, QJsonObject{{QStringLiteral("profileId"), QString()}},
        [this, ch](bool ok, const QJsonValue &data, const QString &msg, int) {
            if (!ok) {
                emit streamFailed(ch, msg.isEmpty() ? tr("取流失败") : msg);
                return;
            }
            const QJsonObject o = data.toObject();
            QString transport;
            const QString url = pickPlayUrl(o, &transport);
            if (url.isEmpty()) {
                emit streamFailed(ch, tr("平台未返回可用流地址"));
                return;
            }
            emit playStarted(ch, QUrl(url), transport);
        });
}

void WebPlatformApi::stopPlay(const ChannelRef &ch)
{
    if (!ch.isValid() || m_http->token().isEmpty()) {
        return;
    }
    // POST /api/v1/resources/{id}/live/stop  (empty JSON body)
    const QString path = QStringLiteral("/api/v1/resources/%1/live/stop")
            .arg(ch.resourceId);
    m_http->postJson(path, QJsonObject(), [ch](bool, const QJsonValue &, const QString &, int) {
        Q_UNUSED(ch)
    });
}

void WebPlatformApi::queryRecordings(const ChannelRef &ch, qint64 startMs, qint64 endMs)
{
    if (!ch.isValid()) {
        emit errorOccurred(QStringLiteral("playback"), tr("通道信息不完整，无法查询云端录像"));
        return;
    }
    const QString path = QStringLiteral("/api/v1/resources/%1/playback/cloud/segments?start=%2&end=%3")
            .arg(ch.resourceId,
                 QString::number(startMs), QString::number(endMs));
    m_http->getJson(path,
        [this, ch](bool ok, const QJsonValue &data, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("playback"),
                                   msg.isEmpty() ? tr("查询录像失败") : msg);
                return;
            }
            QList<RecordingSegment> segs;
            for (const QJsonValue &v : data.toArray()) {
                const QJsonObject o = v.toObject();
                RecordingSegment s;
                s.recordId = qlonglong(o.value(QStringLiteral("recordId")).toVariant().toLongLong());
                s.startMs = qint64(o.value(QStringLiteral("startTime")).toVariant().toLongLong());
                s.endMs = qint64(o.value(QStringLiteral("endTime")).toVariant().toLongLong());
                s.durationMs = qint64(o.value(QStringLiteral("durationMs")).toVariant().toLongLong());
                s.container = o.value(QStringLiteral("container")).toString();
                s.size = qint64(o.value(QStringLiteral("size")).toVariant().toLongLong());
                segs.append(s);
            }
            emit recordingsReady(ch, segs);
        });
}

void WebPlatformApi::startPlayback(const ChannelRef &ch, qlonglong recordId, qint64 startMs)
{
    if (recordId <= 0) {
        emit errorOccurred(QStringLiteral("playback"), tr("无效的录像片段"));
        return;
    }
    const QString path = QStringLiteral("/api/v1/resources/%1/playback/cloud/start?recordId=%2&startTime=%3")
            .arg(ch.resourceId, QString::number(recordId), QString::number(startMs));
    m_http->getJson(path,
        [this, ch, recordId, startMs](bool ok, const QJsonValue &data,
                                      const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("playback"),
                                   msg.isEmpty() ? tr("云端回放起播失败") : msg);
                return;
            }
            const QJsonObject o = data.toObject();
            QString transport;
            const QString url = pickPlayUrl(o, &transport);
            if (url.isEmpty()) {
                emit errorOccurred(QStringLiteral("playback"), tr("平台未返回可用回放地址"));
                return;
            }
            const qint64 dur = qint64(o.value(QStringLiteral("duration")).toVariant().toDouble());
            emit playbackStarted(ch, recordId, QUrl(url), transport, startMs, dur);
        });
}

void WebPlatformApi::stopPlayback(const ChannelRef &ch, qlonglong recordId)
{
    if (recordId <= 0 || !ch.isValid()) return;
    const QString path = QStringLiteral("/api/v1/resources/%1/playback/cloud/stop?recordId=%2")
            .arg(ch.resourceId, QString::number(recordId));
    m_http->getJson(path,
        [this, ch, recordId](bool, const QJsonValue &, const QString &, int) {
            emit playbackStopped(ch, recordId);
        });
}

void WebPlatformApi::ptzControl(const ChannelRef &ch, const PtzCommand &cmd)
{
    if (!ch.isValid()) {
        return;
    }
    // speed 1..10 -> pan/tilt 25..250 (0..255), zoom 2..16 (0..16)
    const int panTilt = qBound(1, cmd.speed, 10) * 25;
    const int zoom = qBound(1, cmd.speed, 10) * 16 / 10;

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("command"), ptzCommandName(cmd.type));
    q.addQueryItem(QStringLiteral("horizonSpeed"), QString::number(panTilt));
    q.addQueryItem(QStringLiteral("verticalSpeed"), QString::number(panTilt));
    q.addQueryItem(QStringLiteral("zoomSpeed"), QString::number(zoom));

    const QString path = QStringLiteral("/api/v1/resources/%1/ptz")
            .arg(ch.resourceId);

    const int cmdType = static_cast<int>(cmd.type);
    const int speed = cmd.speed;
    m_http->getJson(path, q,
        [this, ch, cmdType, speed](bool ok, const QJsonValue &, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("ptz"), msg);
                return;
            }
            emit ptzAcknowledged(ch, cmdType, speed);
        });
}

QString WebPlatformApi::ptzCommandName(PtzCommand::Type type)
{
    switch (type) {
    case PtzCommand::Up:        return QStringLiteral("up");
    case PtzCommand::Down:      return QStringLiteral("down");
    case PtzCommand::Left:      return QStringLiteral("left");
    case PtzCommand::Right:     return QStringLiteral("right");
    case PtzCommand::UpLeft:    return QStringLiteral("upleft");
    case PtzCommand::UpRight:   return QStringLiteral("upright");
    case PtzCommand::DownLeft:  return QStringLiteral("downleft");
    case PtzCommand::DownRight: return QStringLiteral("downright");
    case PtzCommand::ZoomIn:    return QStringLiteral("zoomin");
    case PtzCommand::ZoomOut:   return QStringLiteral("zoomout");
    case PtzCommand::Stop:
    default:                    return QStringLiteral("stop");
    }
}

void WebPlatformApi::queryPresets(const ChannelRef &ch)
{
    if (!ch.isValid()) {
        return;
    }
    const QString path = QStringLiteral("/api/v1/resources/%1/preset/query")
            .arg(ch.resourceId);
    m_http->getJson(path,
        [this, ch](bool ok, const QJsonValue &data, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("preset"), msg);
                emit presetsReady(ch, {});
                return;
            }
            QList<PresetInfo> list;
            if (data.isArray()) {
                for (const QJsonValue &v : data.toArray()) {
                    const QJsonObject o = v.toObject();
                    PresetInfo p;
                    p.id = o.value(QStringLiteral("presetId")).toVariant().toInt();
                    p.name = o.value(QStringLiteral("presetName")).toString();
                    if (p.name.isEmpty()) {
                        p.name = tr("预置位 %1").arg(p.id);
                    }
                    list.append(p);
                }
            }
            emit presetsReady(ch, list);
        });
}

void WebPlatformApi::callPreset(const ChannelRef &ch, int presetId)
{
    if (!ch.isValid()) {
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("presetId"), QString::number(presetId));
    const QString path = QStringLiteral("/api/v1/resources/%1/preset/call")
            .arg(ch.resourceId);
    m_http->getJson(path, q,
        [this](bool ok, const QJsonValue &, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("preset"), msg);
            }
        });
}

void WebPlatformApi::addPreset(const ChannelRef &ch, int presetId)
{
    if (!ch.isValid()) {
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("presetId"), QString::number(presetId));
    const QString path = QStringLiteral("/api/v1/resources/%1/preset/add")
            .arg(ch.resourceId);
    m_http->getJson(path, q,
        [this, ch](bool ok, const QJsonValue &, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("preset"), msg);
                return;
            }
            queryPresets(ch);
        });
}

void WebPlatformApi::deletePreset(const ChannelRef &ch, int presetId)
{
    if (!ch.isValid()) {
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("presetId"), QString::number(presetId));
    const QString path = QStringLiteral("/api/v1/resources/%1/preset/delete")
            .arg(ch.resourceId);
    m_http->getJson(path, q,
        [this, ch](bool ok, const QJsonValue &, const QString &msg, int) {
            if (!ok) {
                emit errorOccurred(QStringLiteral("preset"), msg);
                return;
            }
            queryPresets(ch);
        });
}

} // namespace zp
