/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkShaderBase_DEFINED
#define SkShaderBase_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypes.h"
#include "include/private/SkNoncopyable.h"

#include <cstddef>
#include <cstdint>
#include <optional>

class SkArenaAlloc;
class SkColorSpace;
class SkImage;
class SkWriteBuffer;
enum SkColorType : int;
enum class SkTileMode;
struct SkDeserialProcs;
struct SkStageRec;

namespace SkShaders {
/**
 * Accumulates shader transforms before emitting CPU raster stages, avoiding a
 * matrix stage for every wrapper. Pending matrices have not yet been applied.
 * The total matrix also includes previously applied transforms and supports
 * image mipmap/filter selection. Coordinate-changing shaders may invalidate it.
 * Android Framework retains its reversed local-matrix concatenation order.
 */
class MatrixRec {
public:
    MatrixRec() = default;

    explicit MatrixRec(const SkMatrix& ctm);

    /**
     * Returns a new MatrixRec that represents the existing total and pending matrix
     * pre-concat'ed with m.
     */
    [[nodiscard]] MatrixRec concat(const SkMatrix& m) const;

    /**
     * Appends a mul by the inverse of the pending local matrix to the pipeline. 'postInv' is an
     * additional matrix to post-apply to the inverted pending matrix. If the pending matrix is
     * not invertible the std::optional result won't have a value and the pipeline will be
     * unmodified.
     */
    [[nodiscard]] std::optional<MatrixRec> apply(const SkStageRec& rec,
                                                 const SkMatrix& postInv = {}) const;

    /** Call to indicate that the mapping from shader to device space is not known. */
    void markTotalMatrixInvalid() { fTotalMatrixIsValid = false; }

    /** Marks the CTM as already applied; can avoid re-seeding the shader unnecessarily. */
    void markCTMApplied() { fCTMApplied = true; }

    /**
     * Indicates whether the total matrix of a MatrixRec passed to a SkShader actually
     * represents the full transform between that shader's coordinate space and device space.
     */
    bool totalMatrixIsValid() const { return fTotalMatrixIsValid; }

    /**
     * Gets the total transform from the current shader's space to device space. This may or
     * may not be valid. Shaders should avoid making decisions based on this matrix if
     * totalMatrixIsValid() is false.
     */
    SkMatrix totalMatrix() const {
        const SkMatrix total = SkMatrix::Concat(fCTM, fTotalLocalMatrix);
        return total;
    }

    /** Gets the inverse of totalMatrix(), if invertible. */
    std::optional<SkMatrix> totalInverse() const {
        const SkMatrix total = totalMatrix();
        const std::optional<SkMatrix> inverse = total.invert();
        return inverse;
    }

    /** Is there a transform that has not yet been applied by a parent shader? */
    bool hasPendingMatrix() const {
        const bool pending = (!fCTMApplied && !fCTM.isIdentity()) ||
                             !fPendingLocalMatrix.isIdentity();
        return pending;
    }

    /** When generating raster pipeline, have the device coordinates been seeded? */
    bool rasterPipelineCoordsAreSeeded() const { return fCTMApplied; }

private:
    MatrixRec(const SkMatrix& ctm,
              const SkMatrix& totalLocalMatrix,
              const SkMatrix& pendingLocalMatrix,
              bool totalIsValid,
              bool ctmApplied)
            : fCTM(ctm)
            , fTotalLocalMatrix(totalLocalMatrix)
            , fPendingLocalMatrix(pendingLocalMatrix)
            , fTotalMatrixIsValid(totalIsValid)
            , fCTMApplied(ctmApplied) {}

    const SkMatrix fCTM{};

    // Concatenation of all local matrices, including those already applied.
    const SkMatrix fTotalLocalMatrix{};

    // The accumulated local matrices from walking down the shader hierarchy that have NOT yet
    // been incorporated into the SkRasterPipeline.
    const SkMatrix fPendingLocalMatrix{};

    bool fTotalMatrixIsValid = true;

    // Tracks whether the CTM has already been applied (and in raster pipeline whether the
    // device coords have been seeded.)
    bool fCTMApplied = false;
};

}  // namespace SkShaders

#define SK_ALL_SHADERS(M) \
    M(Blend)              \
    M(CTM)                \
    M(Color)              \
    M(ColorFilter)        \
    M(CoordClamp)         \
    M(Empty)              \
    M(GradientBase)       \
    M(Image)              \
    M(LocalMatrix)        \
    M(PerlinNoise)        \
    M(Picture)            \
    M(Runtime)            \
    M(Transform)          \
    M(TriColor)           \
    M(WorkingColorSpace)

#define SK_ALL_GRADIENTS(M) \
    M(Conical)              \
    M(Linear)               \
    M(Radial)               \
    M(Sweep)

class SkShaderBase : public SkShader {
public:
    ~SkShaderBase() override;

    sk_sp<SkShader> makeInvertAlpha() const;
    sk_sp<SkShader> makeWithCTM(const SkMatrix&) const;  // owns its own ctm

    /**
     *  Returns true if the shader is guaranteed to produce only a single color
     *  If the color parameter is non-null, it is filled in with that color.
     *  Subclasses can override this to allow loop-hoisting optimization.
     */
    virtual bool isConstant(SkColor4f* color = nullptr) const { return false; }

    enum class ShaderType {
#define M(type) k##type,
        SK_ALL_SHADERS(M)
#undef M
    };

    virtual ShaderType type() const = 0;

    enum class GradientType {
        kNone,
#define M(type) k##type,
        SK_ALL_GRADIENTS(M)
#undef M
    };

    /**
     *  If the shader subclass can be represented as a gradient, asGradient
     *  returns the matching GradientType enum (or GradientType::kNone if it
     *  cannot). Also, if info is not null, asGradient populates info with
     *  the relevant (see below) parameters for the gradient.  fColorCount
     *  is both an input and output parameter.  On input, it indicates how
     *  many entries in fColors and fColorOffsets can be used, if they are
     *  non-NULL.  After asGradient has run, fColorCount indicates how
     *  many color-offset pairs there are in the gradient.  If there is
     *  insufficient space to store all of the color-offset pairs, fColors
     *  and fColorOffsets will not be altered.  fColorOffsets specifies
     *  where on the range of 0 to 1 to transition to the given color.
     *  The meaning of fPoint and fRadius is dependent on the type of gradient.
     *
     *  None:
     *      info is ignored.
     *  Color:
     *      fColorOffsets[0] is meaningless.
     *  Linear:
     *      fPoint[0] and fPoint[1] are the end-points of the gradient
     *  Radial:
     *      fPoint[0] and fRadius[0] are the center and radius
     *  Conical:
     *      fPoint[0] and fRadius[0] are the center and radius of the 1st circle
     *      fPoint[1] and fRadius[1] are the center and radius of the 2nd circle
     *  Sweep:
     *      fPoint[0] is the center of the sweep.
     *      fPoint[1] x is the scale, y is the bias
     */
    struct GradientInfo {
        int         fColorCount    = 0;        //!< In-out parameter, specifies passed size
                                               //   of fColors/fColorOffsets on input, and
                                               //   actual number of colors/offsets on
                                               //   output.
        SkColor4f*  fColors        = nullptr;  //!< The colors in the gradient.
        SkScalar*   fColorOffsets  = nullptr;  //!< The unit offset for color transitions.
        SkPoint     fPoint[2]{};                 //!< Type specific, see above.
        SkScalar    fRadius[2]{};                //!< Type specific, see above.
        SkTileMode  fTileMode{};
        bool        fPremulInterp = false;
    };

    virtual GradientType asGradient(GradientInfo* info    = nullptr,
                                    SkMatrix* localMatrix = nullptr) const {
        return GradientType::kNone;
    }

    enum Flags {
        //!< set if all of the colors will be opaque
        kOpaqueAlpha_Flag = 1 << 0,
    };

    /**
     *  ContextRec acts as a parameter bundle for creating Contexts.
     */
    struct ContextRec {
        ContextRec(SkAlpha paintAlpha,
                   const SkShaders::MatrixRec& matrixRec,
                   SkColorType dstColorType,
                   SkColorSpace* dstColorSpace,
                   const SkSurfaceProps& props)
                : fMatrixRec(matrixRec)
                , fDstColorType(dstColorType)
                , fDstColorSpace(dstColorSpace)
                , fProps(props)
                , fPaintAlpha(paintAlpha) {}

        static ContextRec Concat(const ContextRec& parentRec, const SkMatrix& localM) {
            const SkShaders::MatrixRec matrix = parentRec.fMatrixRec.concat(localM);
            const ContextRec result{parentRec.fPaintAlpha, matrix, parentRec.fDstColorType,
                                    parentRec.fDstColorSpace, parentRec.fProps};
            return result;
        }

        const SkShaders::MatrixRec fMatrixRec;
        SkColorType                fDstColorType;   // the color type of the dest surface
        SkColorSpace*              fDstColorSpace;  // the color space of the dest surface (if any)
        SkSurfaceProps             fProps;          // props of the dest surface
        SkAlpha                    fPaintAlpha;

        bool isLegacyCompatible(SkColorSpace* shadersColorSpace) const;
    };

    class Context : public ::SkNoncopyable {
    public:
        Context(const SkShaderBase& shader, const ContextRec&);
        Context(const SkShaderBase&&, const ContextRec&) = delete;

        virtual ~Context();

        /**
         *  Called sometimes before drawing with this shader. Return the type of
         *  alpha your shader will return. The default implementation returns 0.
         *  Your subclass should override if it can (even sometimes) report a
         *  non-zero value, since that will enable various blitters to perform
         *  faster.
         */
        virtual uint32_t getFlags() const { return 0; }

        /**
         *  Called for each span of the object being drawn. Your subclass should
         *  set the appropriate colors (with premultiplied alpha) that correspond
         *  to the specified device coordinates.
         */
        virtual void shadeSpan(int x, int y, SkPMColor[], int count) = 0;

    protected:
        // The shader owner must outlive this arena-owned context.
        const SkShaderBase& fShader;

        uint8_t getPaintAlpha() const { return fPaintAlpha; }

    private:
        uint8_t fPaintAlpha;
    };

    /**
     * Make a context using the memory provided by the arena.
     *
     * @return pointer to context or nullptr if can't be created
     */
    Context* makeContext(const ContextRec&, SkArenaAlloc*) const;

    /**
     *  If the shader can represent its "average" luminance in a single color, return true and
     *  if color is not NULL, return that color. If it cannot, return false and ignore the color
     *  parameter.
     *
     *  Note: if this returns true, the returned color will always be opaque, as only the RGB
     *  components are used to compute luminance.
     */
    bool asLuminanceColor(SkColor4f*) const;

    /**
     * If this returns false, then we draw nothing (do not fall back to shader context). This should
     * only be called on a root-level effect. It assumes that the initial device coordinates have
     * not yet been seeded.
     */
    [[nodiscard]] bool appendRootStages(const SkStageRec& rec, const SkMatrix& ctm) const;

    /**
     * Adds stages to implement this shader. To ensure that the correct input coords are present
     * in r,g MatrixRec::apply() must be called unless the shader needs no coordinates.
     */
    virtual bool appendStages(const SkStageRec&, const SkShaders::MatrixRec&) const = 0;

    virtual SkImage* onIsAImage(SkMatrix*, SkTileMode[2]) const {
        return nullptr;
    }


    static Type GetFlattenableType() { return kSkShader_Type; }
    Type getFlattenableType() const override { return kSkShader_Type; }

    static sk_sp<SkShaderBase> Deserialize(const void* data, size_t size,
                                             const SkDeserialProcs* procs = nullptr) {
        sk_sp<SkFlattenable> decoded = SkFlattenable::Deserialize(kSkShader_Type, data, size, procs);
        SkShaderBase* shader = static_cast<SkShaderBase*>(decoded.release());
        sk_sp<SkShaderBase> result(shader);
        return result;
    }

    /** DEPRECATED. skbug.com/40040221
     *  If this shader can be represented by another shader + a localMatrix, return that shader and
     *  the localMatrix. If not, return nullptr and ignore the localMatrix parameter.
     */
    virtual sk_sp<SkShader> makeAsALocalMatrixShader(SkMatrix* localMatrix) const;

    static SkMatrix ConcatLocalMatrices(const SkMatrix& parentLM, const SkMatrix& childLM) {
#if defined(SK_BUILD_FOR_ANDROID_FRAMEWORK)  // b/256873449
        const SkMatrix combined = SkMatrix::Concat(childLM, parentLM);
#else
        const SkMatrix combined = SkMatrix::Concat(parentLM, childLM);
#endif
        return combined;
    }

protected:
    SkShaderBase();

    void flatten(SkWriteBuffer&) const override;

#ifdef SK_ENABLE_LEGACY_SHADERCONTEXT
    /**
     * Specialize creating a SkShader context using the supplied allocator.
     * @return pointer to context owned by the arena allocator.
     */
    virtual Context* onMakeContext(const ContextRec&, SkArenaAlloc*) const {
        return nullptr;
    }
#endif

    virtual bool onAsLuminanceColor(SkColor4f*) const {
        return false;
    }

private:
    friend class SkShaders::MatrixRec;
};
inline SkShaderBase* as_SB(SkShader* shader) {
    SkShaderBase* base = static_cast<SkShaderBase*>(shader);
    return base;
}

inline const SkShaderBase* as_SB(const SkShader* shader) {
    const SkShaderBase* base = static_cast<const SkShaderBase*>(shader);
    return base;
}

inline const SkShaderBase* as_SB(const sk_sp<SkShader>& shader) {
    const SkShaderBase* base = static_cast<const SkShaderBase*>(shader.get());
    return base;
}

const SkShaderBase* as_SB(const sk_sp<SkShader>&&) = delete;

#endif // SkShaderBase_DEFINED
