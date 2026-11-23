#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "ui/login_dialog.h"
#include "platform/platform_api.h"
#include "platform/web_platform_api.h"
#include "platform/mock_platform_api.h"
#include "player/media_player.h"
#include "analysis/analyzer.h"
#include "train/dataset_store.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QDialog>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace {

// ---------------------------------------------------------------------------
// Dev only: ZP_LOG=<file> mirrors every qDebug/qInfo/qWarning into a file.
// The app is a GUI-subsystem binary, so on Windows nothing written by Qt's
// default handler reaches a redirected stderr; this is the only way to see
// what the media engine / analyzer actually reported when launched from a
// shell or a shortcut.
// ---------------------------------------------------------------------------
QFile *g_logFile = nullptr;
QMutex g_logMutex;

void fileMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = "info";
    switch (type) {
    case QtDebugMsg: level = "debug"; break;
    case QtInfoMsg: level = "info"; break;
    case QtWarningMsg: level = "warn"; break;
    case QtCriticalMsg: level = "crit"; break;
    case QtFatalMsg: level = "fatal"; break;
    }
    QMutexLocker lock(&g_logMutex);
    if (!g_logFile) {
        return;
    }
    QTextStream ts(g_logFile);
    ts.setCodec("UTF-8");
    ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
       << " [" << level << "] " << msg << '\n';
    ts.flush();
}

void installFileLogger()
{
    const QString path = qEnvironmentVariable("ZP_LOG");
    if (path.isEmpty() || g_logFile) {
        return;
    }
    // Leaked on purpose: the handler may still fire while Qt itself unwinds.
    g_logFile = new QFile(path);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
        return;
    }
    qInstallMessageHandler(fileMessageHandler);
}

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
    // A modal dialog (e.g. settings) takes precedence so it can be inspected.
    QWidget *shotTarget() const
    {
        if (QWidget *modal = QApplication::activeModalWidget()) return modal;
        // The training tool is a non-modal window; give it priority so it can
        // be captured headlessly (ZP_TRAINTOOL + ZP_SHOT).
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w->isVisible() && w->objectName() == QLatin1String("TrainingTool")) return w;
        }
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
        // ZP_SHOT_DELAY=<ms> lets a test that waits for AI results (~1-3 s after
        // playback starts) grab a later frame; default matches the old timing.
        int delayMs = 1200;
        if (!qEnvironmentVariableIsEmpty("ZP_SHOT_DELAY")) {
            delayMs = qMax(0, qEnvironmentVariableIntValue("ZP_SHOT_DELAY"));
        }
        QTimer::singleShot(delayMs, this, [this, path]() {
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
    installFileLogger();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("zero-player"));
    app.setOrganizationName(QStringLiteral("zero"));
    app.setWindowIcon(QIcon(zp::icons::appLogo(256)));

    qRegisterMetaType<zp::ChannelRef>("zp::ChannelRef");
    qRegisterMetaType<zp::Session>("zp::Session");
    qRegisterMetaType<zp::DeviceNode>("zp::DeviceNode");
    qRegisterMetaType<zp::PresetInfo>("zp::PresetInfo");
    qRegisterMetaType<zp::MediaState>("zp::MediaState");
    qRegisterMetaType<zp::AnalysisResult>("zp::AnalysisResult");

    // Dev: ZP_DATASET_EXPORT=<dir> exports the current dataset to YOLO layout
    // and exits — lets the export/remap path be verified headlessly.
    const QString exportDir = qEnvironmentVariable("ZP_DATASET_EXPORT");
    if (!exportDir.isEmpty()) {
        QString err;
        const bool ok = zp::DatasetStore::instance().exportYolo(exportDir, 0.2, &err);
        qInfo().noquote() << "[export]" << (ok ? "OK" : "FAILED") << err;
        return ok ? 0 : 1;
    }

    zp::ThemeManager::instance().apply(app);
    // Dev: ZP_THEME=light|dark pins the theme for headless screenshots.
    const QString themeOverride = qEnvironmentVariable("ZP_THEME");
    zp::ThemeManager::instance().setPreset(
        themeOverride.isEmpty() ? zp::SessionManager::instance().theme() : themeOverride);

    AppController controller(app);
    controller.start();

    return app.exec();
}
