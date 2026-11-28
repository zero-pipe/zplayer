#include "platform/mock_platform_api.h"

#include <QFile>
#include <QTimer>
#include <cstdlib>

namespace zp {

namespace {

DeviceNode makeChannel(const QString &deviceId, const QString &channelId,
                       const QString &name, bool online = true, bool ptz = true)
{
    DeviceNode n;
    n.id = QStringLiteral("channel%1").arg(channelId);
    n.name = name;
    n.type = QStringLiteral("channel");
    n.online = online;
    n.ptzCapable = ptz;
    // Demo resourceId = the GB channel id (numeric-ish); keeps ChannelRef valid.
    n.ref.resourceId = channelId;
    n.ref.deviceId = deviceId;
    n.ref.channelId = channelId;
    return n;
}

DeviceNode makeGroup(const QString &id, const QString &name)
{
    DeviceNode n;
    n.id = id;
    n.name = name;
    n.type = QStringLiteral("group");
    n.online = true;
    n.ptzCapable = false;
    return n;
}

} // namespace

MockPlatformApi::MockPlatformApi(QObject *parent)
    : IPlatformApi(parent)
{
}

QString MockPlatformApi::token() const
{
    return m_token;
}

void MockPlatformApi::login(const QString &, const QString &username,
                            const QString &)
{
    QTimer::singleShot(250, this, [this, username]() {
        m_user = username.isEmpty() ? QStringLiteral("demo") : username;
        m_token = QStringLiteral("mock-token");
        Session s;
        s.username = m_user;
        s.serverId = QStringLiteral("demo");
        s.valid = true;
        emit loginSucceeded(s);
    });
}

void MockPlatformApi::logout()
{
    m_token.clear();
}

QList<DeviceNode> MockPlatformApi::channelsFor(const QString &groupId) const
{
    if (groupId == QLatin1String("gA")) {
        return {
            makeChannel("34020000001320000000", "34020000001320000001", "A区-大门-球机"),
            makeChannel("34020000001320000000", "34020000001320000002", "A区-停车场-枪机"),
            makeChannel("34020000001320000000", "34020000001320000003", "A区-电梯厅-半球", true, false),
            makeChannel("34020000001320000000", "34020000001320000004", "A区-周界-枪机"),
        };
    }
    if (groupId == QLatin1String("gB")) {
        return {
            makeChannel("34020000001320000010", "34020000001320000011", "B区-仓库入口-球机"),
            makeChannel("34020000001320000010", "34020000001320000012", "B区-装卸区-枪机"),
            makeChannel("34020000001320000010", "34020000001320000013", "B区-办公走廊-半球", false, false),
        };
    }
    return {};
}

void MockPlatformApi::fetchChildren(const QString &parentId)
{
    QTimer::singleShot(120, this, [this, parentId]() {
        QList<DeviceNode> nodes;
        if (parentId.isEmpty()) {
            nodes << makeGroup(QStringLiteral("gA"), QString::fromUtf8("园区 A"))
                  << makeGroup(QStringLiteral("gB"), QString::fromUtf8("园区 B"));
        } else {
            nodes = channelsFor(parentId);
        }
        emit childrenReady(parentId, nodes);
    });
}

QString MockPlatformApi::demoUrl() const
{
    const QByteArray env = qgetenv("ZERO_PLAYER_DEMO_FILE");
    if (!env.isEmpty()) {
        const QString path = QString::fromLocal8Bit(env);
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

void MockPlatformApi::startPlay(const ChannelRef &ch)
{
    QTimer::singleShot(200, this, [this, ch]() {
        const QString local = demoUrl();
        const QUrl url = local.isEmpty()
                ? QUrl(QStringLiteral("rtsp://10.0.0.1:8554/live/%1_%2")
                           .arg(ch.deviceId, ch.channelId))
                : QUrl::fromLocalFile(local);
        emit playStarted(ch, url, local.isEmpty() ? QStringLiteral("RTSP")
                                                  : QStringLiteral("FILE"));
    });
}

void MockPlatformApi::stopPlay(const ChannelRef &ch)
{
    emit playStopped(ch);
}

void MockPlatformApi::ptzControl(const ChannelRef &ch, const PtzCommand &cmd)
{
    QTimer::singleShot(80, this, [this, ch, cmd]() {
        emit ptzAcknowledged(ch, static_cast<int>(cmd.type), cmd.speed);
    });
}

void MockPlatformApi::queryPresets(const ChannelRef &ch)
{
    QTimer::singleShot(80, this, [this, ch]() {
        QList<PresetInfo> list;
        for (int i = 1; i <= 3; ++i) {
            PresetInfo p;
            p.id = i;
            p.name = tr("预置位 %1").arg(i);
            list.append(p);
        }
        emit presetsReady(ch, list);
    });
}

void MockPlatformApi::callPreset(const ChannelRef &ch, int presetId)
{
    Q_UNUSED(ch)
    Q_UNUSED(presetId)
}

void MockPlatformApi::addPreset(const ChannelRef &ch, int presetId)
{
    Q_UNUSED(presetId)
    QTimer::singleShot(50, this, [this, ch]() { queryPresets(ch); });
}

void MockPlatformApi::deletePreset(const ChannelRef &ch, int presetId)
{
    Q_UNUSED(presetId)
    QTimer::singleShot(50, this, [this, ch]() { queryPresets(ch); });
}

void MockPlatformApi::queryRecordings(const ChannelRef &ch, qint64 startMs, qint64 endMs)
{
    Q_UNUSED(startMs)
    Q_UNUSED(endMs)
    // 演示模式无云录像：返回空列表。
    QTimer::singleShot(50, this, [this, ch]() {
        emit recordingsReady(ch, QList<RecordingSegment>());
    });
}

void MockPlatformApi::startPlayback(const ChannelRef &ch, qlonglong recordId, qint64 startMs)
{
    Q_UNUSED(startMs)
    const QUrl url(demoUrl());
    QTimer::singleShot(50, this, [this, ch, recordId, url, startMs]() {
        emit playbackStarted(ch, recordId, url, QStringLiteral("演示"), startMs, 60000);
    });
}

void MockPlatformApi::stopPlayback(const ChannelRef &ch, qlonglong recordId)
{
    QTimer::singleShot(20, this, [this, ch, recordId]() {
        emit playbackStopped(ch, recordId);
    });
}

} // namespace zp
