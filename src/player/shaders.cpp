#include "player/shaders.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace zp {

namespace {

// Unsharp-mask style edge enhancement. It runs on the GPU as an mpv user
// shader: cheap, engine-side, and it sharpens whatever the picture is —
// including the ROI-zoomed view, which is where soft upscaling shows most.
const char *const kSharpenShader = R"GLSL(
//!DESC zero-player sharpen
//!HOOK MAIN
//!BIND HOOKED
//!COMPONENTS 4

vec4 hook() {
    vec4 c = HOOKED_tex(HOOKED_pos);
    vec2 px = HOOKED_pt;
    vec4 blur = (HOOKED_tex(HOOKED_pos + vec2(0.0, -px.y)) +
                 HOOKED_tex(HOOKED_pos + vec2(0.0,  px.y)) +
                 HOOKED_tex(HOOKED_pos + vec2(-px.x, 0.0)) +
                 HOOKED_tex(HOOKED_pos + vec2( px.x, 0.0))) * 0.25;
    vec4 sharp = c + (c - blur) * 0.9;
    return clamp(sharp, 0.0, 1.0);
}
)GLSL";

} // namespace

QString ensureSharpenShaderFile()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) {
        dir = QDir::tempPath();
    }
    if (dir.isEmpty()) {
        return QString();
    }
    const QString path = QDir(dir).filePath(QStringLiteral("zp_sharpen.glsl"));
    QFile f(path);
    if (f.exists() && f.size() > 0) {
        return path;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    f.write(kSharpenShader);
    f.close();
    return path;
}

} // namespace zp
