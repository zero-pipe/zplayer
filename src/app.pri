SOURCES += \
    main.cpp \
    app/PlayerWindow.cpp \
    app/SessionManager.cpp \
    platform/platform_http_client.cpp \
    platform/web_platform_api.cpp \
    platform/mock_platform_api.cpp \
    player/media_factory.cpp \
    player/video_tile.cpp \
    player/video_grid.cpp \
    ui/theme.cpp \
    ui/icons.cpp \
    ui/toast.cpp \
    ui/icon_field.cpp \
    ui/login_dialog.cpp \
    ui/device_tree_view.cpp \
    ui/ptz_joystick.cpp \
    ui/ptz_panel.cpp \
    ui/video_settings_dialog.cpp

HEADERS += \
    app/PlayerWindow.h \
    app/SessionManager.h \
    platform/platform_api.h \
    platform/platform_http_client.h \
    platform/web_platform_api.h \
    platform/mock_platform_api.h \
    player/media_player.h \
    player/media_factory.h \
    player/video_tile.h \
    player/video_grid.h \
    ui/theme.h \
    ui/icons.h \
    ui/toast.h \
    ui/icon_field.h \
    ui/login_dialog.h \
    ui/device_tree_view.h \
    ui/ptz_joystick.h \
    ui/ptz_panel.h \
    ui/video_settings_dialog.h

# ---- Media engine implementations (guarded by app.pro CONFIG) ----
contains(DEFINES, ZP_WITH_MPV) {
    SOURCES += player/backends/mpv_media_player.cpp
    HEADERS += player/backends/mpv_media_player.h
    # qthelper.hpp is a header-only helper, included by mpv_media_player.cpp.
}

contains(DEFINES, ZP_WITH_QTAV) {
    SOURCES += player/backends/qtav_media_player.cpp
    HEADERS += player/backends/qtav_media_player.h
}

RESOURCES += resources/resources.qrc
