#ifndef ZP_MOCK_PLATFORM_API_H
#define ZP_MOCK_PLATFORM_API_H

#include "platform/platform_api.h"

namespace zp {

// Offline demo implementation of IPlatformApi. Serves an in-memory tree and
// returns a playable URL per channel:
//   - if ZERO_PLAYER_DEMO_FILE points to a local video, that file is returned
//   - otherwise a placeholder rtsp:// URL is returned
// Login always succeeds (any username/password).
class MockPlatformApi : public IPlatformApi {
    Q_OBJECT
public:
    explicit MockPlatformApi(QObject *parent = nullptr);

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

private:
    QList<DeviceNode> channelsFor(const QString &groupId) const;
    QString demoUrl() const;
    QString m_token;
    QString m_user;
};

} // namespace zp

#endif // ZP_MOCK_PLATFORM_API_H
