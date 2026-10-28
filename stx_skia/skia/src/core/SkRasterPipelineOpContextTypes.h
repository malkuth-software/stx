/*
 * Copyright 2026 stx_skia contributors
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterPipelineOpContextTypes_DEFINED
#define SkRasterPipelineOpContextTypes_DEFINED

#include "modules/skcms/skcms.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"

#include <cstdint>
#include <type_traits>

// Each raster pipeline op's context, as an explicit specialization of SkRasterPipelineOpContext.
// The table says what SkRasterPipeline::append() accepts for the op and what each precision's
// stage kernel receives. Both sides are checked against it: append() in Release, and every
// kernel at compile time, because SkRasterPipeline_opts.h instantiates each stage with a kernel
// pointer whose context parameter is the entry's type. An op without an entry fails to compile
// wherever the table is read.
//
// Context kinds:
//  - None: the stage ignores the context field.
//  - Immediate: the field holds a value packed by append(op, uintptr_t), not an address.
//  - Pointer to Object: callers pass an Object pointer. A const Object is read-only: callers may
//    pass a const pointer and stages may not write it. A non-const Object must arrive mutable,
//    because a stage writes it or run() repoints it for row tails (memory contexts). Each
//    precision's stage receives exactly its listed object type; memory contexts are read
//    through const.
//  - Lane storage: callers pass float storage sized for a highp program; a lowp program,
//    lowp throughout, reads and writes the same storage as uint16_t lanes.

enum class SkRasterPipelineContextKind { kNone, kImmediate, kPointer };

template <SkRasterPipelineContextKind kKind, typename Append, typename Highp, typename Lowp>
struct SkRasterPipelineContextSpec {
    static constexpr SkRasterPipelineContextKind kind = kKind;
    using AppendObject = Append;
    using HighpObject = Highp;
    using LowpObject = Lowp;
};

using SkRasterPipelineNoContext =
        SkRasterPipelineContextSpec<SkRasterPipelineContextKind::kNone, void, void, void>;
using SkRasterPipelineImmediateContext =
        SkRasterPipelineContextSpec<SkRasterPipelineContextKind::kImmediate, void, void, void>;
template <typename Object>
using SkRasterPipelinePointerContext =
        SkRasterPipelineContextSpec<SkRasterPipelineContextKind::kPointer, Object, Object, Object>;
// Appended mutable, read through const by the stages: the memory contexts run() repoints.
template <typename Object>
using SkRasterPipelineConstStageContext =
        SkRasterPipelineContextSpec<SkRasterPipelineContextKind::kPointer, Object, const Object,
                                    const Object>;
template <typename HighpLane, typename LowpLane>
using SkRasterPipelineLaneContext =
        SkRasterPipelineContextSpec<SkRasterPipelineContextKind::kPointer, HighpLane, HighpLane,
                                    LowpLane>;

template <SkRasterPipelineOp op>
struct SkRasterPipelineOpContext;

template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::move_src_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::move_dst_src>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::swap_src_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clamp_01>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clamp_a_01>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clamp_gamut>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::premul>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::premul_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::force_opaque>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::force_opaque_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::set_rgb>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::swap_rb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::swap_rb_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::black_color>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::white_color>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::uniform_color>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::UniformColorCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::uniform_color_dst>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::UniformColorCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::seed_shader>
        : SkRasterPipelineNoContext {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_a8>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_a8_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_a8>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_a8>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_565>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_565_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_565>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_565>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_4444>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_4444_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_4444>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_4444>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_8888>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_8888_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_8888>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_8888>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rg88>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rg88_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_rg88>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_rg88>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_r8>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alpha_to_gray>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alpha_to_gray_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alpha_to_red>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alpha_to_red_dst>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bt709_luminance_or_luma_to_alpha>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bt709_luminance_or_luma_to_rgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilerp_clamp_8888>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_src>
        : SkRasterPipelineLaneContext<const float, const uint16_t> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_src>
        : SkRasterPipelineLaneContext<float, uint16_t> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_src_a>
        : SkRasterPipelineLaneContext<float, uint16_t> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_dst>
        : SkRasterPipelineLaneContext<const float, const uint16_t> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_dst>
        : SkRasterPipelineLaneContext<float, uint16_t> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::scale_u8>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::scale_565>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::scale_1_float>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::scale_native>
        : SkRasterPipelineLaneContext<const float, const uint16_t> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::lerp_u8>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::lerp_565>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::lerp_1_float>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::lerp_native>
        : SkRasterPipelineLaneContext<const float, const uint16_t> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::dstatop>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::dstin>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::dstout>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::dstover>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::srcatop>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::srcin>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::srcout>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::srcover>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clear>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::modulate>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::multiply>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::plus_>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::screen>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xor_>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::darken>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::difference>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::exclusion>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::hardlight>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::lighten>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::overlay>
        : SkRasterPipelineNoContext {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::srcover_rgba_8888>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_translate>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_scale_translate>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_2x3>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_perspective>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::decal_x>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::DecalTileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::decal_y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::DecalTileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::decal_x_and_y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::DecalTileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::check_decal_mask>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::DecalTileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clamp_x_1>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mirror_x_1>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::repeat_x_1>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::clamp_x_and_y>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::CoordClampCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::evenly_spaced_gradient>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GradientCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gradient>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GradientCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::evenly_spaced_2_stop_gradient>
        : SkRasterPipelinePointerContext<
                  const SkRasterPipelineContexts::EvenlySpaced2StopGradientCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_unit_angle>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_radius>
        : SkRasterPipelineNoContext {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::emboss>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::EmbossCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::swizzle>
        : SkRasterPipelineImmediateContext {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_x>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_y>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_r>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_g>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_b>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_a>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_r_255>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_g_255>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_b_255>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::debug_a_255>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::callback>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::CallbackCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::unbounded_set_rgb>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::unbounded_uniform_color>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::UniformColorCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::unpremul>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::unpremul_polar>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::dither>
        : SkRasterPipelinePointerContext<const float> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_16161616>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_16161616_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_16161616>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_16161616>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_a16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_a16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_a16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_a16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_r16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_r16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_r16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_r16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rg1616>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rg1616_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_rg1616>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_rg1616>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_f16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_f16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_f16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_f16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_af16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_af16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_af16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_af16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rf16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rf16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_rf16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_rf16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rgf16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_rgf16_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_rgf16>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_rgf16>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_f32>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_f32_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_f32>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_f32>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_1010102>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_1010102_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_1010102>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_1010102>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_1010102_xr>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_1010102_xr_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_1010102_xr>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_1010102_xr>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_10x6>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_10x6_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_10x6>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_10x6>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gather_10101010_xr>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_10101010_xr>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_10101010_xr_dst>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
// run() repoints this memory context for row tails.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_10101010_xr>
        : SkRasterPipelineConstStageContext<SkRasterPipelineContexts::MemoryCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::store_src_rg>
        : SkRasterPipelinePointerContext<float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::load_src_rg>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::byte_tables>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::TablesCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::colorburn>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::colordodge>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::softlight>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::hue>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::saturation>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::color>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::luminosity>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_3x3>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_3x4>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_4x5>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::matrix_4x3>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::parametric>
        : SkRasterPipelinePointerContext<const skcms_TransferFunction> {};
// The stage reads the transfer function's exponent, g.
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gamma_>
        : SkRasterPipelinePointerContext<const skcms_TransferFunction> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::PQish>
        : SkRasterPipelinePointerContext<const skcms_TransferFunction> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::HLGish>
        : SkRasterPipelinePointerContext<const skcms_TransferFunction> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::HLGinvish>
        : SkRasterPipelinePointerContext<const skcms_TransferFunction> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::ootf>
        : SkRasterPipelinePointerContext<const float> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::rgb_to_hsl>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::hsl_to_rgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_lab_to_xyz>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_oklab_to_linear_srgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_oklab_gamut_map_to_linear_srgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_hcl_to_lab>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_hsl_to_srgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::css_hwb_to_srgb>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::gauss_a_to_rgba>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mirror_x>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::TileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::repeat_x>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::TileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mirror_y>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::TileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::repeat_y>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::TileCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::negate_x>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilerp_clamp_8888_force_highp>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_clamp_8888>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::GatherCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilinear_setup>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilinear_nx>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilinear_px>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilinear_ny>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bilinear_py>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_setup>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_n3x>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_n1x>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_p1x>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_p3x>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_n3y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_n1y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_p1y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::bicubic_p3y>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::accumulate>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::SamplerCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::perlin_noise>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::PerlinNoiseCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mipmap_linear_init>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::MipmapCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mipmap_linear_update>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::MipmapCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mipmap_linear_finish>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::MipmapCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_2pt_conical_strip>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_2pt_conical_focal_on_circle>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_2pt_conical_well_behaved>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_2pt_conical_smaller>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::xy_to_2pt_conical_greater>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alter_2pt_conical_compensate_focal>
        : SkRasterPipelinePointerContext<const SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::alter_2pt_conical_unswap>
        : SkRasterPipelineNoContext {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mask_2pt_conical_nan>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::mask_2pt_conical_degenerates>
        : SkRasterPipelinePointerContext<SkRasterPipelineContexts::Conical2PtCtx> {};
template <> struct SkRasterPipelineOpContext<SkRasterPipelineOp::apply_vector_mask>
        : SkRasterPipelinePointerContext<const uint32_t> {};

// The pointer type a stage receives for its context: const void* for None (NoCtx), void* for
// Immediate, and a pointer to the precision's object for Pointer.
template <typename Spec, bool kHighp>
struct SkRasterPipelineStageContextPointerOf {
    using Object = std::conditional_t<kHighp, typename Spec::HighpObject, typename Spec::LowpObject>;
    using type = std::conditional_t<Spec::kind == SkRasterPipelineContextKind::kNone, const void*,
                 std::conditional_t<Spec::kind == SkRasterPipelineContextKind::kImmediate, void*,
                                    Object*>>;
};
template <typename Spec, bool kHighp>
using SkRasterPipelineStageContextPointer =
        typename SkRasterPipelineStageContextPointerOf<Spec, kHighp>::type;

#endif  // SkRasterPipelineOpContextTypes_DEFINED
