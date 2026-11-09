#pragma once
// The gradient factories return SkGradientShaderResult in the candidate and sk_sp<SkShader> in
// the reference profile (STX_SKIA_REFERENCE); shader_of gives the shader either way, so a test
// that only draws with the shader compiles against both.

#include "include/effects/SkGradient.h"

#include <utility>

#ifdef STX_SKIA_REFERENCE
inline sk_sp<SkShader> shader_of(sk_sp<SkShader> made) {
    return made;
}
#else
inline sk_sp<SkShader> shader_of(SkGradientShaderResult made) {
    sk_sp<SkShader> shader = std::move(made.shader);
    return shader;
}
#endif
