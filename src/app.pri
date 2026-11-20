SOURCES += \
    main.cpp \
    app/PlayerWindow.cpp \
    app/SessionManager.cpp \
    analysis/analysis_service.cpp \
    analysis/byte_tracker.cpp \
    analysis/ocr_detector.cpp \
    analysis/timeline.cpp \
    analysis/yolo_detector.cpp \
    analysis/evidence.cpp \
    platform/platform_http_client.cpp \
    platform/web_platform_api.cpp \
    platform/mock_platform_api.cpp \
    player/media_factory.cpp \
    player/frame_grabber.cpp \
    player/shaders.cpp \
    player/video_tile.cpp \
    player/video_grid.cpp \
    ui/theme.cpp \
    ui/icons.cpp \
    ui/ui_kit.cpp \
    ui/analysis_overlay.cpp \
    ui/timeline_slider.cpp \
    ui/evidence_dialog.cpp \
    ui/toast.cpp \
    ui/icon_field.cpp \
    ui/login_dialog.cpp \
    ui/device_tree_view.cpp \
    ui/ptz_panel.cpp \
    ui/video_settings_page.cpp \
    ui/settings_dialog.cpp \
    train/dataset_store.cpp \
    train/annotation_canvas.cpp \
    train/training_tool_window.cpp \
    train/model_registry.cpp

HEADERS += \
    app/PlayerWindow.h \
    app/SessionManager.h \
    analysis/analyzer.h \
    analysis/analysis_service.h \
    analysis/byte_tracker.h \
    analysis/ocr_detector.h \
    analysis/timeline.h \
    analysis/yolo_detector.h \
    analysis/evidence.h \
    platform/platform_api.h \
    platform/platform_http_client.h \
    platform/web_platform_api.h \
    platform/mock_platform_api.h \
    player/media_player.h \
    player/media_factory.h \
    player/frame_grabber.h \
    player/shaders.h \
    player/video_tile.h \
    player/video_grid.h \
    ui/theme.h \
    ui/icons.h \
    ui/ui_kit.h \
    ui/analysis_overlay.h \
    ui/timeline_slider.h \
    ui/evidence_dialog.h \
    ui/toast.h \
    ui/icon_field.h \
    ui/login_dialog.h \
    ui/device_tree_view.h \
    ui/ptz_panel.h \
    ui/video_settings_page.h \
    ui/settings_dialog.h \
    train/dataset_store.h \
    train/annotation_canvas.h \
    train/training_tool_window.h \
    train/model_registry.h

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
