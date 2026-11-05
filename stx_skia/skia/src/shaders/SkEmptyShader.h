/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkEmptyShader_DEFINED
#define SkEmptyShader_DEFINED

#include "src/shaders/SkShaderBase.h"

#include "include/core/SkFlattenable.h"

class SkReadBuffer;
class SkWriteBuffer;
struct SkStageRec;

/**
 *  \class SkEmptyShader
 *  A shader that contributes no raster stages and draws nothing.
 */
class SkEmptyShader : public SkShaderBase {
public:
    SkEmptyShader() {}

protected:
    void flatten(SkWriteBuffer& buffer) const override {
        // Empty shaders have no payload.
    }

    bool appendStages(const SkStageRec&, const SkShaders::MatrixRec&) const override {
        return false;
    }

    ShaderType type() const override { return ShaderType::kEmpty; }
    bool isOpaque() const override { return false; }

private:
    SK_FLATTENABLE_HOOKS(SkEmptyShader)
};

#endif  // SkEmptyShader_DEFINED
