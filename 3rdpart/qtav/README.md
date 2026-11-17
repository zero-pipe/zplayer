# QtAV — media engine (3rdparty, trimmed)

This directory contains the **media engine** of [QtAV](https://github.com/wang-bin/QtAV)
(version 1.13.0), vendored as a third-party library for **zero-player**.

It is **not** a full copy of upstream QtAV. Only the reusable media-engine modules
were carried over so that zero-player can build its own player framework on top.

## Provenance

- Upstream: https://github.com/wang-bin/QtAV
- Version: 1.13.0 (`.qmake.conf`)
- License: **LGPL v2.1 or later** — see `COPYING.LGPLv2.1.txt`.
  Dynamic linking is expected; keep QtAV as a shared library and ship the license
  notice to satisfy LGPL obligations for commercial products.

## What is kept (module map)

| Directory | Module | Responsibility |
|---|---|---|
| `src/` | Core engine (`libQtAV`) | `AVPlayer` (playback control), demuxing (`AVDemuxer`), audio/video threads, clock sync (`AVClock`), frames/packets, `VideoCapture` (snapshot), `VideoFrameExtractor` (thumbnail), `AVTranscoder` (record/convert), `MediaIO` (custom stream input), statistics |
| `src/QtAV/` | Public headers | The library API (`AVPlayer.h`, `VideoRenderer.h`, `VideoCapture.h`, `MediaIO.h`, ...) |
| `src/codec/` | Codecs | FFmpeg software decoders + hardware decoders (DXVA2, D3D11VA, CUDA, VAAPI, VideoToolbox/VDA, MediaCodec, CrystalHD, MMAL, QSV) and encoders |
| `src/output/video/` | Video output | Renderer backends: OpenGL, QPainter (software), plus surface interop |
| `src/output/audio/` | Audio output | Backends: DSound, XAudio2, OpenAL, PortAudio, PulseAudio, OpenSL, AudioToolbox, null |
| `src/filter/` | Filters | Audio/video filter chain, libavfilter, GLSL/custom shaders (for OSD/watermark/masking) |
| `src/subtitle/` | Subtitles | Subtitle decoding/rendering |
| `src/opengl/`, `src/shaders/` | GPU rendering | OpenGL video renderer + built-in shaders |
| `src/directx/`, `src/cuda/`, `src/vaapi/` | Platform accel | Direct2D, CUDA, VAAPI helpers |
| `src/utils/`, `src/compat/` | Support | Utilities, compiler/platform compat |
| `src/i18n/` | i18n | UI/translation resources |
| `widgets/` | `libQtAVWidgets` (Qt Widgets) | Widget/QWidget renderers: `GLWidgetRenderer2`, `OpenGLWidgetRenderer`, `GraphicsItemRenderer`, `VideoPreviewWidget`, `VulkanRenderer` |
| `qml/` | `libQmlAV` (Qt Quick, optional) | QML `Video` element and helpers; only built when the Quick module is available |
| `config.tests/`, `tests/arch/` | Build support | qmake dependency/arch detection used by `root.pri`/`configure.pri` |
| `contrib/` | Build support | `uchardet` helper and D3D/XAudio2 SDK headers |

## What was dropped (intentionally)

- `examples/` — the sample players (`player`, `videowall`, `videogroup`, ...).
  The `videowall`/`videogroup` approach for split-screen is a useful reference; if you
  need it, pull it from upstream rather than vendoring here.
- `tools/`, `tests/` (all except `tests/arch`), `doc/`, `debian/`, `msys2/`,
  `qtc_packaging/`, `python/`, `.github/`, CI configs.
- `contrib/capi` (git submodule, C API) — add explicitly if a C API is ever needed.
- The packaging target (`pack.pri` / `.deb` install) was removed from `QtAV.pro`.

## Building this module

Builds with qmake exactly like upstream. Tested with Qt 5.14.2 (msvc2017, 32-bit)
and FFmpeg 4.2.1 (win32 dev).

```bat
call "...\VC\Auxiliary\Build\vcvars32.bat"
set PATH=<qt-mingw/msvc bin>;%PATH%
set INCLUDE=<ffmpeg>\include;%INCLUDE%
set LIB=<ffmpeg>\lib;%LIB%
qmake "F:\OpenSource\zero-player\3rdpart\qtav\QtAV.pro" -r CONFIG+=release CONFIG+=no-examples
nmake            :: builds libQtAV, libQtAVWidgets, libQmlAV
```

FFmpeg headers/libs must be on the compiler's `INCLUDE`/`LIB` path (or installed
into the Qt prefix). FFmpeg **4.x** is the safest match for this codebase; newer
FFmpeg removed APIs it relies on.

## Known limitations (for zero-player planning)

- Project is upstream-unmaintained (author moved to `mdk-sdk`).
- No split-screen UI, no PTZ (ONVIF/vendor), no web-platform client, no device
  management — those belong to zero-player's own layers. QtAV only provides the
  media pipeline (decode/render/audio/capture/extract/transcode/filter).
- Each `AVPlayer` is its own pipeline (threads); plan hardware decoding and
  visible-channel-only decoding for many simultaneous channels.
