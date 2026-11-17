@echo off
setlocal enabledelayedexpansion

REM ===== Toolchain / dependency paths (edit for your machine) =====
set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat"
set "QT_BIN=D:\Qt\Qt5.14.2\5.14.2\msvc2017\bin"
set "FFMPEG=F:\06.chenqw\4_ffplay\ffmpeg-4.2.1-win32-dev"
set "FFMPEG_DLLS=F:\06.chenqw\4_ffplay\dll"
REM ================================================================

set "ROOT=%~dp0"
set "ENGINE=%ROOT%3rdpart\qtav\_build"
set "AINBUILD=%ROOT%build"

call "%VCVARS%" || goto :err
set "PATH=%QT_BIN%;%PATH%"
set "INCLUDE=%FFMPEG%\include;%INCLUDE%"
set "LIB=%FFMPEG%\lib;%LIB%"

echo ============================================================
echo [1/2] Building QtAV engine...
echo ============================================================
if not exist "%ENGINE%" mkdir "%ENGINE%"
pushd "%ENGINE%"
"%QT_BIN%\qmake.exe" "..\QtAV.pro" -r CONFIG+=release CONFIG+=no-examples || goto :err
nmake /NOLOGO || goto :err
popd

echo ============================================================
echo [2/2] Building zero-player...
echo ============================================================
if not exist "%AINBUILD%" mkdir "%AINBUILD%"
pushd "%AINBUILD%"
"%QT_BIN%\qmake.exe" -cache "%ENGINE%\.qmake.cache" "%ROOT%src\app.pro" CONFIG+=release || goto :err
nmake /NOLOGO || goto :err
popd

set "OUTBIN=%ENGINE%\bin"
echo.
echo Build OK. Output: %OUTBIN%\zero-player.exe

echo Deploying Qt runtime + FFmpeg DLLs...
"%QT_BIN%\windeployqt.exe" --release --no-translations "%OUTBIN%\zero-player.exe" >nul 2>&1
rem Qt5OpenGL is linked but not always auto-detected; copy it explicitly.
if exist "%QT_BIN%\Qt5OpenGL.dll" copy /y "%QT_BIN%\Qt5OpenGL.dll" "%OUTBIN%" >nul
if exist "%FFMPEG_DLLS%" copy /y "%FFMPEG_DLLS%\*.dll" "%OUTBIN%" >nul
echo Done. Run: "%OUTBIN%\zero-player.exe"
echo (Optional) set ZERO_PLAYER_DEMO_FILE=path\to\video.mp4 to play a local sample.
exit /b 0

:err
echo.
echo BUILD FAILED (errorlevel=%ERRORLEVEL%)
exit /b 1
