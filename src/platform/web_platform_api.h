#ifndef ZP_WEB_PLATFORM_API_H
#define ZP_WEB_PLATFORM_API_H

#include "platform/platform_api.h"

#include <QHash>

namespace zp {

class PlatformHttpClient;

// Real IPlatformApi implementation talking to zero-web-server over HTTP/JSON.
// Endpoint mapping is documented in the class .cpp header comment.
class WebPlatformApi : public IPlatformApi {
    Q_OBJECT
public:
    explicit WebPlatformApi(QObject *parent = nullptr);

    void login(const QString &baseUrl, const QString &username,
               const QString &password) override;
    void logout() override;
    QString token() const override;

    void fetchChildren(const QString &parentId) override;

    void startPlay(const zp::ChannelRef &ch) override;
    void stopPlay(const zp::ChannelRef &ch) override;

    void ptzControl(const zp::ChannelRef &ch, const zp::PtzCommand &cmd) override;

    void queryPresets(const zp::ChannelRef &ch) override;
    void callPreset(const zp::ChannelRef &ch, int presetId) override;
    void addPreset(const zp::ChannelRef &ch, int presetId) override;
    void deletePreset(const zp::ChannelRef &ch, int presetId) override;

    void queryRecordings(const zp::ChannelRef &ch, qint64 startMs,
                         qint64 endMs) override;
    void startPlayback(const zp::ChannelRef &ch, qlonglong recordId,
                       qint64 startMs) override;
    void seekPlayback(const zp::ChannelRef &ch, qlonglong recordId,
                      qint64 seekMs) override;
    void stopPlayback(const zp::ChannelRef &ch, qlonglong recordId) override;

private:
    static QList<DeviceNode> parseNodes(const QJsonValue &data);
    static QString ptzCommandName(PtzCommand::Type type);

    PlatformHttpClient *m_http = nullptr;
    QString m_serverId;
    QString m_username;

    // GB channel id -> real parent GB device id. The group tree only exposes
    // the organization code in parentDeviceId, which /play/start rejects
    // ("设备不存在"), so the real device is resolved via /common/channel/one.
    QHash<QString, QString> m_channelDevices;
};

} // namespace zp

#endif // ZP_WEB_PLATFORM_API_H
