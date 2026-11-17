#include "app/MainWindow.h"
#include "platform/mock_platform_api.h"

#include <QApplication>
#include <QPalette>
#include <QColor>

namespace {

void applyDarkTheme(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));

    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(0x1b, 0x22, 0x2b));
    pal.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Base, QColor(0x14, 0x1a, 0x21));
    pal.setColor(QPalette::AlternateBase, QColor(0x1b, 0x22, 0x2b));
    pal.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Button, QColor(0x24, 0x2d, 0x37));
    pal.setColor(QPalette::ButtonText, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Highlight, QColor(0x21, 0x96, 0xf3));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x24, 0x2d, 0x37));
    pal.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
    app.setPalette(pal);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("zero-player"));

    qRegisterMetaType<zp::DeviceNode>("zp::DeviceNode");

    applyDarkTheme(app);

    auto *api = new zp::MockPlatformApi(&app);
    zp::MainWindow win(api);
    win.show();

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (!args.at(i).startsWith(QLatin1Char('-'))) {
            win.openDirect(QUrl::fromUserInput(args.at(i)));
            break;
        }
    }

    return app.exec();
}
