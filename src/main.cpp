#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/theme.h"
#include "ui/login_dialog.h"
#include "platform/platform_api.h"
#include "platform/web_platform_api.h"
#include "platform/mock_platform_api.h"
#include "player/media_player.h"
#include "analysis/analyzer.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QDialog>

namespace {

// Drives the login -> player -> logout cycle.
class AppController : public QObject {
public:
    explicit AppController(QApplication &app) : m_app(app) {}

    void start()
    {
        // Dev/demo hook: jump straight into the player with mock data.
        if (!qEnvironmentVariableIsEmpty("ZP_DEMO")) {
            beginLogin(QString(), QStringLiteral("demo"), QString(), true);
            return;
        }
        // Dev auto-login: ZP_BASE + ZP_USER + ZP_PASS -> log in against a real server.
        const QString autoBase = qEnvironmentVariable("ZP_BASE");
        if (!autoBase.isEmpty()) {
            beginLogin(autoBase, qEnvironmentVariable("ZP_USER"),
                       qEnvironmentVariable("ZP_PASS"), false);
            return;
        }
        showLogin();
    }

private:
    void showLogin()
    {
        m_login = new zp::LoginDialog();
        m_login->setAuthHandler([this](const QString &baseUrl, const QString &user,
                                       const QString &pwd, bool offline) {
            beginLogin(baseUrl, user, pwd, offline);
        });
        // Centre on screen.
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect g = screen->availableGeometry();
            m_login->move(g.center() - QPoint(m_login->width() / 2, m_login->height() / 2));
        }
        connect(m_login, &QDialog::finished, this, [this](int r) {
            if (r != QDialog::Accepted) {
                m_app.quit();
            }
        });
        m_login->show();
        scheduleShot();
    }

    // Dev only: grab a widget to a PNG after it has laid out (ZP_SHOT=path).
    QWidget *shotTarget() const
    {
        if (m_player) return m_player;
        if (m_login) return m_login;
        return nullptr;
    }
    void scheduleShot()
    {
        const QString path = qEnvironmentVariable("ZP_SHOT");
        if (path.isEmpty()) {
            return;
        }
        QTimer::singleShot(1200, this, [this, path]() {
            if (QWidget *w = shotTarget()) {
                w->grab().save(path, "PNG");
            }
        });
    }

    void beginLogin(const QString &baseUrl, const QString &user,
                    const QString &pwd, bool offline)
    {
        if (m_api) {
            m_api->deleteLater();
            m_api = nullptr;
        }
        m_offline = offline;
        if (offline) {
            m_api = new zp::MockPlatformApi(this);
        } else {
            m_api = new zp::WebPlatformApi(this);
        }

        connect(m_api, &zp::IPlatformApi::loginSucceeded, this,
                [this, baseUrl, user, pwd](const zp::Session &session) {
                    zp::SessionManager::instance().rememberOnSuccess(
                        baseUrl, user, pwd, m_login && m_login->rememberPassword());
                    m_session = session;
                    if (m_login) {
                        m_login->accept();
                    }
                    openPlayer();
                });
        connect(m_api, &zp::IPlatformApi::loginFailed, this, [this](const QString &msg) {
            if (m_login) {
                m_login->setBusy(false);
                m_login->showError(msg);
            }
        });

        m_api->login(baseUrl, user, pwd);
    }

    void openPlayer()
    {
        m_player = new zp::PlayerWindow(m_api, m_session);
        connect(m_player, &zp::PlayerWindow::logoutRequested, this, [this]() {
            if (m_player) {
                m_player->close();
                m_player->deleteLater();
                m_player = nullptr;
            }
            if (m_api) {
                m_api->logout();
            }
            if (m_login) {
                m_login->deleteLater();
                m_login = nullptr;
            }
            showLogin();
        });
        m_player->show();

        // Dev: fill N demo tiles (for layout verification).
        const QString fillCount = qEnvironmentVariable("ZP_FILL");
        if (!fillCount.isEmpty()) {
            const QString file = qEnvironmentVariable("ZP_FILE");
            const QUrl url = file.isEmpty()
                    ? QUrl(QStringLiteral("rtsp://127.0.0.1:8554/live/demo"))
                    : QUrl::fromLocalFile(file);
            const QString layout = qEnvironmentVariable("ZP_LAYOUT");
            if (!layout.isEmpty()) {
                m_player->setDemoLayout(layout.toInt());
            }
            m_player->fillDemo(url, fillCount.toInt());
        }

        // Command-line direct media target.
        const QStringList args = m_app.arguments();
        for (int i = 1; i < args.size(); ++i) {
            if (!args.at(i).startsWith(QLatin1Char('-'))) {
                m_player->openDirect(QUrl::fromUserInput(args.at(i)));
                break;
            }
        }

        if (m_login) {
            m_login->deleteLater();
            m_login = nullptr;
        }
        scheduleShot();
    }

    QApplication &m_app;
    QPointer<zp::LoginDialog> m_login;
    QPointer<zp::PlayerWindow> m_player;
    zp::IPlatformApi *m_api = nullptr;
    zp::Session m_session;
    bool m_offline = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("zero-player"));
    app.setOrganizationName(QStringLiteral("zero"));

    qRegisterMetaType<zp::ChannelRef>("zp::ChannelRef");
    qRegisterMetaType<zp::Session>("zp::Session");
    qRegisterMetaType<zp::DeviceNode>("zp::DeviceNode");
    qRegisterMetaType<zp::PresetInfo>("zp::PresetInfo");
    qRegisterMetaType<zp::MediaState>("zp::MediaState");
    qRegisterMetaType<zp::AnalysisResult>("zp::AnalysisResult");

    zp::ThemeManager::instance().apply(app);
    zp::ThemeManager::instance().setPreset(zp::SessionManager::instance().theme());

    AppController controller(app);
    controller.start();

    return app.exec();
}
