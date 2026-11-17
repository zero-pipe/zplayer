TEMPLATE = app
TARGET = zero-player
CONFIG += c++11
CONFIG -= app_bundle

QT += core gui widgets opengl network

msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

# ---- Media engines ----
# Primary: libmpv (prebuilt SDK, x64). QtAV is a legacy fallback and only
# compiled when CONFIG+=with_qtav is passed (requires an x86 toolchain + the
# x86-only qtav-sdk). The QtAV adapter sources are always present in the tree.

# libmpv SDK (default on)
isEmpty(MPV_SDK_DIR): MPV_SDK_DIR = $$clean_path($$PWD/../3rdpart/mpv-sdk)
exists($$MPV_SDK_DIR/mpv-sdk.pri) {
    include($$MPV_SDK_DIR/mpv-sdk.pri)
    DEFINES += ZP_WITH_MPV
} else {
    warning("mpv-sdk not found at $$MPV_SDK_DIR; media engine will be unavailable")
}

# QtAV SDK (opt-in)
with_qtav {
    isEmpty(QTAV_SDK_DIR): QTAV_SDK_DIR = $$clean_path($$PWD/../3rdpart/qtav-sdk)
    exists($$QTAV_SDK_DIR/qtav-sdk.pri) {
        include($$QTAV_SDK_DIR/qtav-sdk.pri)
        DEFINES += ZP_WITH_QTAV
    } else {
        error("CONFIG+=with_qtav but qtav-sdk not found at $$QTAV_SDK_DIR")
    }
}

INCLUDEPATH += $$PWD
include(app.pri)
