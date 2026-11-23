@echo off
REM ===========================================================================
REM  zero-player build script (x64 / libmpv)
REM
REM  Toolchain paths are auto-detected. To override on your machine, either
REM  set the environment variables below before running, or create a
REM  build.local.bat next to this file (git-ignored) that sets them, e.g.:
REM
REM      set "QT_BIN=D:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin"
REM      set "VCVARS=D:\VS\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
REM
REM  See build.local.bat.example.
REM ===========================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
pushd "%ROOT%" >nul

REM ---- 1. Optional per-machine overrides (git-ignored) ----
if exist "%ROOT%build.local.bat" call "%ROOT%build.local.bat"

REM ---- 2. Locate vcvars64 ----
if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)
if not defined VCVARS (
    echo [ERROR] vcvars64.bat not found. Set VCVARS in build.local.bat.
    goto :err
)

REM ---- 3. Locate Qt (msvc2017_64) ----
if not defined QT_BIN (
    for %%Q in ("C:\Qt\Qt5.14.2\5.14.2\msvc2019_64" "D:\Qt\Qt5.14.2\5.14.2\msvc2019_64" "C:\Qt\Qt5.14.2\5.14.2\msvc2017_64" "D:\Qt\Qt5.14.2\5.14.2\msvc2017_64") do (
        if not defined QT_BIN if exist "%%~Q\bin\qmake.exe" set "QT_BIN=%%~Q\bin"
    )
)
if not defined QT_BIN (
    echo [ERROR] Qt MSVC x64 kit not found. Set QT_BIN in build.local.bat.
    goto :err
)

set "MPV_SDK=%ROOT%3rdpart\mpv-sdk"
set "BUILDDIR=%ROOT%build"
set "OUTBIN=%ROOT%out"

call "%VCVARS%" || goto :err
set "PATH=%QT_BIN%;%PATH%"

echo ============================================================
echo Building zero-player (x64, libmpv)
echo   Qt   : %QT_BIN%
echo   mpv  : %MPV_SDK%
echo ============================================================

if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
pushd "%BUILDDIR%"
"%QT_BIN%\qmake.exe" "%ROOT%src\app.pro" CONFIG+=release MPV_SDK_DIR="%MPV_SDK%" || goto :err
nmake /NOLOGO || goto :err
popd

set "APPEXE=%BUILDDIR%\release\zero-player.exe"
if not exist "%APPEXE%" set "APPEXE=%BUILDDIR%\zero-player.exe"
if not exist "%APPEXE%" (
    echo [ERROR] build produced no zero-player.exe
    goto :err
)

if not exist "%OUTBIN%" mkdir "%OUTBIN%"
copy /y "%APPEXE%" "%OUTBIN%\" >nul

echo Deploying Qt runtime + libmpv + dependencies...
"%QT_BIN%\windeployqt.exe" --release --no-translations "%OUTBIN%\zero-player.exe" >nul 2>&1
if exist "%QT_BIN%\Qt5OpenGL.dll" copy /y "%QT_BIN%\Qt5OpenGL.dll" "%OUTBIN%" >nul
REM libmpv + its runtime dependency DLLs (ffmpeg, libass, ...).
if exist "%MPV_SDK%\bin\x64" copy /y "%MPV_SDK%\bin\x64\*.dll" "%OUTBIN%" >nul
REM ONNX Runtime (DirectML) + the bundled YOLO model, so AI works from out/.
if exist "%ROOT%3rdpart\onnxruntime\bin\*.dll" copy /y "%ROOT%3rdpart\onnxruntime\bin\*.dll" "%OUTBIN%" >nul
if exist "%ROOT%3rdpart\models" (
    if not exist "%OUTBIN%\models" mkdir "%OUTBIN%\models"
    xcopy /y /q /e "%ROOT%3rdpart\models\*" "%OUTBIN%\models\" >nul
)

popd >nul
echo.
echo Build OK. Run: "%OUTBIN%\zero-player.exe"
exit /b 0

:err
popd >nul 2>&1
echo.
echo BUILD FAILED
exit /b 1
