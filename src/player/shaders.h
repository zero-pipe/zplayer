#ifndef ZP_SHADERS_H
#define ZP_SHADERS_H

#include <QString>

namespace zp {

// Writes the built-in sharpening shader (mpv user-shader GLSL) to a stable
// path under the temp dir and returns that path. Empty on failure.
QString ensureSharpenShaderFile();

} // namespace zp

#endif // ZP_SHADERS_H
