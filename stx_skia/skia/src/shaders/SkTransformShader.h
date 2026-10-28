/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkTransformShader_DEFINED
#define SkTransformShader_DEFINED

#include "include/core/SkScalar.h"
#include "include/private/SkAssert.h"
#include "src/shaders/SkShaderBase.h"

#include <array>

class SkMatrix;
struct SkStageRec;

// SkTransformShader applies a matrix transform to the shader coordinates, like a local matrix
// shader. The difference with a typical local matrix shader is that this shader's matrix is
// not combined with the inverse CTM or other local matrices in order to facilitate modifying the
// matrix between uses of the raster pipeline. This supports drawVertices and drawAtlas, in
// which the mapping from each triangle (when explicit texture coords are used) or atlas quad to
// shader space is different.
class SkTransformShader : public SkShaderBase {
public:
    // The child and this wrapper must outlive every pipeline built from them.
    explicit SkTransformShader(const SkShaderBase& shader, bool allowPerspective);
    SkTransformShader(const SkShaderBase&&, bool) = delete;

    // Adds a pipestage to multiply the incoming coords in 'r' and 'g' by the matrix. The child
    // shader is called with no pending local matrix and the total transform as unknowable.
    bool appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const override;

    // Change the matrix used by the generated SkRasterPipeline.
    bool update(const SkMatrix& matrix);

    ShaderType type() const override { return ShaderType::kTransform; }

    // These are never serialized/deserialized
    Factory getFactory() const override {
        SkDEBUGFAIL("SkTransformShader shouldn't be serialized.");
        return {};
    }
    const char* getTypeName() const override {
        SkDEBUGFAIL("SkTransformShader shouldn't be serialized.");
        return nullptr;
    }

    bool isOpaque() const override {
        const bool opaque = fShader.isOpaque();
        return opaque;
    }

private:
    const SkShaderBase& fShader;
    std::array<SkScalar, 9> fMatrixStorage{}; // Row-major matrix borrowed by raster stages.
    bool fAllowPerspective;
};
#endif  //SkTransformShader_DEFINED
