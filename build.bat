@echo off
setlocal enabledelayedexpansion

REM ===== Toolchain / dependency paths (edit for your machine) =====
set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
set "QT_BIN=D:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin"
REM ================================================================

set "ROOT=%~dp0"
set "MPV_SDK=%ROOT%3rdpart\mpv-sdk"
set "QTAV_SDK=%ROOT%3rdpart\qtav-sdk"
set "BUILDDIR=%ROOT%build"
set "OUTBIN=%ROOT%out"

call "%VCVARS%" || goto :err
set "PATH=%QT_BIN%;%PATH%"

echo ============================================================
echo Building zero-player (x64, libmpv)...
echo ============================================================
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
pushd "%BUILDDIR%"
"%QT_BIN%\qmake.exe" "%ROOT%src\app.pro" CONFIG+=release MPV_SDK_DIR="%MPV_SDK%" || goto :err
nmake /NOLOGO || goto :err
popd

set "APPEXE=%BUILDDIR%\release\zero-player.exe"
if not exist "%APPEXE%" set "APPEXE=%BUILDDIR%\zero-player.exe"

if not exist "%OUTBIN%" mkdir "%OUTBIN%"
copy /y "%APPEXE%" "%OUTBIN%\" >nul

echo Deploying Qt runtime + libmpv + dependencies...
"%QT_BIN%\windeployqt.exe" --release --no-translations "%OUTBIN%\zero-player.exe" >nul 2>&1
if exist "%QT_BIN%\Qt5OpenGL.dll" copy /y "%QT_BIN%\Qt5OpenGL.dll" "%OUTBIN%" >nul
REM libmpv + its runtime dependency DLLs (ffmpeg, libass, ...).
if exist "%MPV_SDK%\bin\x64" copy /y "%MPV_SDK%\bin\x64\*.dll" "%OUTBIN%" >nul

echo.
echo Build OK. Run: "%OUTBIN%\zero-player.exe"
exit /b 0

:err
echo.
echo BUILD FAILED (errorlevel=%ERRORLEVEL%)
exit /b 1
