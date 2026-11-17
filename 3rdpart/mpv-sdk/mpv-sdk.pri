# mpv-sdk.pri — libmpv prebuilt SDK (x64 only)
#
# libmpv-2.dll is built by MSYS2 ucrt64 (GCC/MinGW-w64, UCRT runtime).
# lib/x64/libmpv-2.lib is an MSVC import library generated from the DLL exports,
# so an MSVC application can link against it via the stable C ABI.
#
# Layout:
#   include/mpv/*.h
#   lib/x64/libmpv-2.lib
#   bin/x64/libmpv-2.dll  (+ runtime dependency DLLs)
#
# NOTE: only x64 is provided. A 32-bit build cannot use this SDK.
#
# Usage in app.pro:
#   MPV_SDK_DIR = $$clean_path($$PWD/../3rdpart/mpv-sdk)
#   include($$MPV_SDK_DIR/mpv-sdk.pri)

isEmpty(MPV_SDK_DIR): MPV_SDK_DIR = $$PWD

!exists($$MPV_SDK_DIR/include/mpv/client.h) {
    error("mpv SDK not found at $$MPV_SDK_DIR (missing include/mpv/client.h)")
}

INCLUDEPATH += $$MPV_SDK_DIR/include

LIBS += -L$$MPV_SDK_DIR/lib/x64 -llibmpv-2

win32 {
    MPV_SDK_BIN = $$MPV_SDK_DIR/bin/x64
}
