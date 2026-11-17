TEMPLATE = app
TARGET = zero-player
CONFIG += c++11
CONFIG -= app_bundle

QT += core gui widgets opengl network

msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

# The QtAV engine is prebuilt into 3rdpart/qtav/_build (see build.bat).
# Override with qmake QTAV_BUILD_DIR=<dir> if you build the engine elsewhere.
isEmpty(QTAV_BUILD_DIR): QTAV_BUILD_DIR = $$clean_path($$PWD/../3rdpart/qtav/_build)
BUILD_DIR = $$QTAV_BUILD_DIR

QTAV_ROOT = $$clean_path($$PWD/../3rdpart/qtav)
include($$QTAV_ROOT/.qmake.conf)
include($$QTAV_ROOT/src/libQtAV.pri)
include($$QTAV_ROOT/widgets/libQtAVWidgets.pri)

INCLUDEPATH += $$PWD
include(app.pri)
