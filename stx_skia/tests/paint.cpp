// Differential corpus for SkPaint and SkPaintPriv: the observable state after every setter
// (including out-of-range enum values, negative and non-finite widths, miter limits and alpha,
// and colors in other color spaces), equality, copies and moves, blend-mode queries,
// nothingToDraw, fast bounds for every style with stroke, path-effect and mask-filter
// combinations, and SkPaintPriv's overwrite, dither and color-filter folding decisions. Both
// builds must produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/effects/SkDashPathEffect.h"
#include "src/core/SkPaintPriv.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "dash_result.hpp"
#include "blur_result.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

template <typename T>
concept HasImageFiltering = requires(T& paint) {
    paint.getImageFilter();
    paint.refImageFilter();
    paint.setImageFilter(nullptr);
};
#ifdef STX_SKIA_REFERENCE
static_assert(HasImageFiltering<SkPaint>);
#else
static_assert(!HasImageFiltering<SkPaint>);

// Paint serialization had no production caller once pictures were removed; each entry point
// is gone.
template <typename Priv, typename Writer>
concept FlattensPaint = requires(const SkPaint& paint, Writer& writer) { Priv::Flatten(paint, writer); };
template <typename Priv, typename Reader>
concept UnflattensPaint = requires(Reader& reader) { Priv::Unflatten(reader); };
template <typename Reader>
concept ReadsPaint = requires(Reader& reader) { reader.readPaint(); };
template <typename Writer>
concept WritesPaint = requires(const SkPaint& paint, Writer& writer) { writer.writePaint(paint); };
static_assert(!FlattensPaint<SkPaintPriv, SkBinaryWriteBuffer>);
static_assert(!UnflattensPaint<SkPaintPriv, SkReadBuffer>);
static_assert(!ReadsPaint<SkReadBuffer>);
static_assert(!WritesPaint<SkBinaryWriteBuffer>);
#endif

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void record_state(Output& output, const SkPaint& paint) {
    append(output, paint.getColor4f());
    append(output, paint.getColor());
    append(output, paint.getAlpha());
    append(output, paint.getAlphaf());
    append(output, paint.getStrokeWidth());
    append(output, paint.getStrokeMiter());
    append(output, static_cast<int>(paint.getStyle()));
    append(output, static_cast<int>(paint.getStrokeCap()));
    append(output, static_cast<int>(paint.getStrokeJoin()));
    append_bool(output, paint.isAntiAlias());
    append_bool(output, paint.isDither());
    const std::optional<SkBlendMode> mode = paint.asBlendMode();
    append_bool(output, mode.has_value());
    append(output, static_cast<int>(mode.value_or(SkBlendMode::kClear)));
    append(output, static_cast<int>(paint.getBlendMode_or(SkBlendMode::kXor)));
    append_bool(output, paint.isSrcOver());
    append_bool(output, paint.nothingToDraw());
    append_bool(output, paint.canComputeFastBounds());
    ++output.cases;
}

void setter_corpus(Output& output) {
    const std::array<float, 9> scalars{0, 0.5f, 1, 2.5f, -1, -0.0f, kInf, -kInf, kNaN};
    for (const float value : scalars) {
        SkPaint paint;
        paint.setStrokeWidth(3);
        paint.setStrokeMiter(5);
        paint.setStrokeWidth(value);
        paint.setStrokeMiter(value);
        paint.setAlphaf(value);
        record_state(output, paint);
    }
    // Every enum value, plus the one past each enumeration's last value, which is rejected.
    for (int style = 0; style <= SkPaint::kStyleCount; ++style) {
        for (int cap = 0; cap <= SkPaint::kCapCount; ++cap) {
            for (int join = 0; join <= SkPaint::kJoinCount; ++join) {
                SkPaint paint;
                paint.setStyle(SkPaint::kStroke_Style);
                paint.setStrokeCap(SkPaint::kRound_Cap);
                paint.setStrokeJoin(SkPaint::kBevel_Join);
                paint.setStyle(static_cast<SkPaint::Style>(style));
                paint.setStrokeCap(static_cast<SkPaint::Cap>(cap));
                paint.setStrokeJoin(static_cast<SkPaint::Join>(join));
                record_state(output, paint);
            }
        }
    }
    const std::array<SkColor4f, 6> colors{SkColor4f{0.25f, 0.5f, 0.75f, 0.5f},
                                          SkColor4f{1.5f, -0.25f, 0.5f, 2.0f},
                                          SkColor4f{0.1f, 0.2f, 0.3f, -1.0f},
                                          SkColor4f{kNaN, 0.5f, 0.5f, kNaN},
                                          SkColor4f{kInf, 0.0f, -kInf, 1.0f},
                                          SkColor4f{0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<sk_sp<SkColorSpace>, 3> spaces{nullptr, SkColorSpace::MakeSRGB(),
                                                    SkColorSpace::MakeSRGBLinear()};
    for (const SkColor4f& color : colors) {
        for (const sk_sp<SkColorSpace>& space : spaces) {
            SkPaint paint(color, space.get());
            record_state(output, paint);
        }
    }
    for (const uint32_t argb : {0x00000000u, 0x80402010u, 0xFFFFFFFFu, 0x01FF00FFu}) {
        SkPaint paint;
        paint.setColor(argb);
        record_state(output, paint);
        paint.setARGB(argb >> 24, (argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
        record_state(output, paint);
        paint.setAntiAlias(true);
        paint.setDither(true);
        paint.setStroke(true);
        record_state(output, paint);
        paint.reset();
        record_state(output, paint);
    }
}

void blend_corpus(Output& output) {
    sk_sp<SkColorFilter> alphaFilter = SkColorFilters::Blend(0x80FF0000, SkBlendMode::kDstIn);
    sk_sp<SkColorFilter> colorFilter = SkColorFilters::Blend(0xFF00FF00, SkBlendMode::kModulate);
    for (int mode = 0; mode <= static_cast<int>(SkBlendMode::kLastMode); ++mode) {
        for (const float alpha : {0.0f, 0.5f, 1.0f}) {
            for (int filter = 0; filter < 3; ++filter) {
                SkPaint paint;
                paint.setBlendMode(static_cast<SkBlendMode>(mode));
                paint.setAlphaf(alpha);
                if (filter == 1) paint.setColorFilter(alphaFilter);
                if (filter == 2) paint.setColorFilter(colorFilter);
                record_state(output, paint);
                for (int override = 0; override < 3; ++override) {
                    const SkPaintPriv::ShaderOverrideOpacity opacity =
                            static_cast<SkPaintPriv::ShaderOverrideOpacity>(override);
                    append_bool(output, SkPaintPriv::Overwrites(&paint, opacity));
                }
            }
        }
    }
    for (int override = 0; override < 3; ++override) {
        const SkPaintPriv::ShaderOverrideOpacity opacity =
                static_cast<SkPaintPriv::ShaderOverrideOpacity>(override);
        append_bool(output, SkPaintPriv::Overwrites(nullptr, opacity));
    }
    SkPaint custom;
    custom.setBlender(SkBlender::Mode(SkBlendMode::kScreen));
    record_state(output, custom);
}

void equality_corpus(Output& output) {
    SkPaint base;
    base.setColor(0x80402010u);
    base.setStrokeWidth(2);
    std::array<SkPaint, 9> variants{base, base, base, base, base, base, base, base, base};
    variants[1].setAntiAlias(true);
    variants[2].setDither(true);
    variants[3].setStyle(SkPaint::kStrokeAndFill_Style);
    variants[4].setStrokeCap(SkPaint::kSquare_Cap);
    variants[5].setStrokeJoin(SkPaint::kRound_Join);
    variants[6].setStrokeMiter(9);
    variants[7].setBlendMode(SkBlendMode::kPlus);
    variants[8].setShader(SkShaders::Color(0xFF123456u));
    for (const SkPaint& a : variants) {
        for (const SkPaint& b : variants) {
            append_bool(output, a == b);
            append_bool(output, a != b);
        }
    }
    SkPaint copied(variants[8]);
    SkPaint moved(std::move(copied));
    append_bool(output, moved == variants[8]);
    SkPaint assigned;
    assigned = variants[5];
    append_bool(output, assigned == variants[5]);
    ++output.cases;
}

void bounds_corpus(Output& output) {
    const float dash[2] = {3, 2};
    sk_sp<SkPathEffect> dashed = effect_of(SkDashPathEffect::Make(SkSpan<const float>(dash, 2), 0));
    sk_sp<SkMaskFilter> blur = filter_of(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 2.5f, false));
    const SkRect rect = SkRect::MakeLTRB(-3.5f, 2, 40.25f, 17);
    for (int style = 0; style < SkPaint::kStyleCount; ++style) {
        for (const float width : {0.0f, 1.0f, 6.5f}) {
            for (int join = 0; join < SkPaint::kJoinCount; ++join) {
                for (int effects = 0; effects < 4; ++effects) {
                    SkPaint paint;
                    paint.setStyle(static_cast<SkPaint::Style>(style));
                    paint.setStrokeWidth(width);
                    paint.setStrokeJoin(static_cast<SkPaint::Join>(join));
                    paint.setStrokeMiter(join == 0 ? 10.0f : 1.0f);
                    if (effects & 1) paint.setPathEffect(dashed);
                    if (effects & 2) paint.setMaskFilter(blur);
                    append_bool(output, paint.canComputeFastBounds());
                    if (paint.canComputeFastBounds()) {
                        SkRect storage = SkRect::MakeEmpty();
                        append(output, paint.computeFastBounds(rect, &storage));
                        SkRect strokeStorage = SkRect::MakeEmpty();
                        append(output, paint.computeFastStrokeBounds(rect, &strokeStorage));
                    }
                    ++output.cases;
                }
            }
        }
    }
}

void priv_corpus(Output& output) {
    const std::array<SkColorType, 6> types{kUnknown_SkColorType, kRGB_565_SkColorType,
                                           kARGB_4444_SkColorType, kRGBA_8888_SkColorType,
                                           kRGBA_F16_SkColorType, kAlpha_8_SkColorType};
    sk_sp<SkShader> constant = SkShaders::Color(0xFF808080u);
    sk_sp<SkMaskFilter> blur = filter_of(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 1.0f, false));
    for (const SkColorType type : types) {
        for (int setup = 0; setup < 4; ++setup) {
            SkPaint paint;
            paint.setDither(setup != 0);
            if (setup == 2) paint.setShader(constant);
            if (setup == 3) paint.setMaskFilter(blur);
            append_bool(output, SkPaintPriv::ShouldDither(paint, type));
        }
    }
    // Folding a color filter into the paint color, or into its shader.
    const float matrix[20] = {0.5f, 0, 0, 0, 0.1f,  0, 1, 0, 0, 0,  0, 0, 0.25f, 0, 0.2f,
                              0, 0, 0, 1, 0};
    const std::array<sk_sp<SkColorSpace>, 2> spaces{nullptr, SkColorSpace::MakeSRGBLinear()};
    for (int withShader = 0; withShader < 2; ++withShader) {
        for (const sk_sp<SkColorSpace>& space : spaces) {
            SkPaint paint;
            paint.setColor(SkColor4f{0.2f, 0.4f, 0.6f, 0.8f}, nullptr);
            paint.setColorFilter(SkColorFilters::Matrix(matrix));
            if (withShader) paint.setShader(constant);
            SkPaintPriv::RemoveColorFilter(&paint, space.get());
            append_bool(output, paint.getColorFilter() == nullptr);
            append_bool(output, paint.getShader() != nullptr);
            record_state(output, paint);
        }
    }
}

}  // namespace

// setAlphaf pins to [0, 1] and makes NaN zero, as SkPaint.h states.
bool alpha_pinning() {
    const std::array<float, 8> given{0.25f, 0.0f, -0.0f, 1.0f, -1.0f, 2.5f, kInf, -kInf};
    const std::array<float, 8> pinned{0.25f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f};
    for (size_t index = 0; index < given.size(); ++index) {
        SkPaint paint;
        paint.setAlphaf(given[index]);
        if (paint.getAlphaf() != pinned[index]) return false;
    }
    SkPaint paint;
    paint.setAlphaf(kNaN);
    const bool nan_is_zero = paint.getAlphaf() == 0.0f && !std::isnan(paint.getColor4f().fA);
    return nan_is_zero;
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    if (!alpha_pinning()) return 3;
    Output output{};
    setter_corpus(output);
    blend_corpus(output);
    equality_corpus(output);
    bounds_corpus(output);
    priv_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("paint: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
