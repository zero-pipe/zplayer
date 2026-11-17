SOURCES += \
    main.cpp \
    app/PlayerWindow.cpp \
    app/SessionManager.cpp \
    platform/platform_http_client.cpp \
    platform/web_platform_api.cpp \
    platform/mock_platform_api.cpp \
    player/video_tile.cpp \
    player/video_grid.cpp \
    ui/theme.cpp \
    ui/icons.cpp \
    ui/toast.cpp \
    ui/icon_field.cpp \
    ui/login_dialog.cpp \
    ui/device_tree_view.cpp \
    ui/ptz_joystick.cpp \
    ui/ptz_panel.cpp

HEADERS += \
    app/PlayerWindow.h \
    app/SessionManager.h \
    platform/platform_api.h \
    platform/platform_http_client.h \
    platform/web_platform_api.h \
    platform/mock_platform_api.h \
    player/video_tile.h \
    player/video_grid.h \
    ui/theme.h \
    ui/icons.h \
    ui/toast.h \
    ui/icon_field.h \
    ui/login_dialog.h \
    ui/device_tree_view.h \
    ui/ptz_joystick.h \
    ui/ptz_panel.h

RESOURCES += resources/resources.qrc
