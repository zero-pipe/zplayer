#ifndef ZP_PLATFORM_API_H
#define ZP_PLATFORM_API_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QList>
#include <QMetaType>

namespace zp {

// A playable / controllable camera channel, addressed by the GB identity pair
// used by the web platform: (deviceId = parent device GB id,
// channelId = channel GB id).
struct ChannelRef {
    QString deviceId;
    QString channelId;
    // 通道库 id（平台 group/tree 的 id 字段）；云端回放按它查片段用。
    int platformId = 0;

    bool isValid() const { return !deviceId.isEmpty() && !channelId.isEmpty(); }
    bool operator==(const ChannelRef &o) const {
        return deviceId == o.deviceId && channelId == o.channelId;
    }
    bool operator!=(const ChannelRef &o) const { return !(*this == o); }
    QString key() const { return deviceId + QLatin1Char('/') + channelId; }
};

// Authenticated session returned by the platform.
struct Session {
    QString username;
    QString serverId;
    bool valid = false;
};

// A PTZ preset slot owned by a channel.
struct PresetInfo {
    int id = 0;
    QString name;
};

// A cloud recording segment (PS or MP4) on the playback timeline.
// Times are absolute epoch milliseconds.
struct RecordingSegment {
    qlonglong recordId = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 durationMs = 0;
    QString container; // "ps" | "mp4"
    qint64 size = 0;
};

// Device tree node delivered by the platform.
struct DeviceNode {
    QString id;          // unique tree key ("group12" / "channel1001" / ...)
    QString name;
    QString type;        // "group" | "channel"
    bool online = true;
    bool ptzCapable = false;
    ChannelRef ref;      // valid only when type == "channel"
    QList<DeviceNode> children;

    bool isGroup() const { return type == QLatin1String("group"); }
    bool isChannel() const { return type == QLatin1String("channel"); }
};

// PTZ command passed through the platform to the device.
struct PtzCommand {
    enum Type {
        Up,
        Down,
        Left,
        Right,
        UpLeft,
        UpRight,
        DownLeft,
        DownRight,
        ZoomIn,
        ZoomOut,
        Stop
    };

    Type type = Stop;
    int speed = 5; // 1..10 (mapped to platform 0..255 / 0..16 internally)
};

// Platform abstraction. The concrete implementation talks to the web platform.
// The player only depends on this interface, so the transport can change freely.
class IPlatformApi : public QObject {
    Q_OBJECT
public:
    explicit IPlatformApi(QObject *parent = nullptr) : QObject(parent) {}
    ~IPlatformApi() override = default;

    // Session / auth
    virtual void login(const QString &baseUrl, const QString &username,
                       const QString &password) = 0;
    virtual void logout() = 0;
    virtual QString token() const = 0;

    // Catalog (lazy). parentId empty => root. Returns direct children.
    virtual void fetchChildren(const QString &parentId) = 0;

    // Streaming
    virtual void startPlay(const zp::ChannelRef &ch) = 0;
    virtual void stopPlay(const zp::ChannelRef &ch) = 0;

    // PTZ (pass-through)
    virtual void ptzControl(const zp::ChannelRef &ch, const zp::PtzCommand &cmd) = 0;

    // Presets
    virtual void queryPresets(const zp::ChannelRef &ch) = 0;
    virtual void callPreset(const zp::ChannelRef &ch, int presetId) = 0;
    virtual void addPreset(const zp::ChannelRef &ch, int presetId) = 0;
    virtual void deletePreset(const zp::ChannelRef &ch, int presetId) = 0;

    // Cloud playback (绝对时间，ms). 片段查询用通道库 id（ch.platformId）。
    virtual void queryRecordings(const zp::ChannelRef &ch, qint64 startMs,
                                 qint64 endMs) = 0;
    // 起播：从该片段的绝对 startMs（可落在段内任意位置）开始。
    virtual void startPlayback(const zp::ChannelRef &ch, qlonglong recordId,
                               qint64 startMs) = 0;
    // 定位：绝对 seekMs（平台换算成段内相对再下沉）。
    virtual void seekPlayback(const zp::ChannelRef &ch, qlonglong recordId,
                              qint64 seekMs) = 0;
    virtual void stopPlayback(const zp::ChannelRef &ch, qlonglong recordId) = 0;

signals:
    void loginSucceeded(const zp::Session &session);
    void loginFailed(const QString &message);
    void childrenReady(const QString &parentId, const QList<zp::DeviceNode> &nodes);
    void playStarted(const zp::ChannelRef &ch, const QUrl &url, const QString &transport);
    void playStopped(const zp::ChannelRef &ch);
    void streamFailed(const zp::ChannelRef &ch, const QString &message);
    void ptzAcknowledged(const zp::ChannelRef &ch, int cmdType, int speed);
    void presetsReady(const zp::ChannelRef &ch, const QList<zp::PresetInfo> &presets);
    void recordingsReady(const zp::ChannelRef &ch,
                         const QList<zp::RecordingSegment> &segments);
    void playbackStarted(const zp::ChannelRef &ch, qlonglong recordId,
                         const QUrl &url, const QString &transport,
                         qint64 segStartMs, qint64 segDurationMs);
    void playbackStopped(const zp::ChannelRef &ch, qlonglong recordId);
    void sessionExpired();
    void errorOccurred(const QString &scope, const QString &message);
};

} // namespace zp

Q_DECLARE_METATYPE(zp::ChannelRef)
Q_DECLARE_METATYPE(zp::Session)
Q_DECLARE_METATYPE(zp::DeviceNode)
Q_DECLARE_METATYPE(zp::PresetInfo)
Q_DECLARE_METATYPE(zp::RecordingSegment)

#endif // ZP_PLATFORM_API_H
