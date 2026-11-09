/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterPipelineOpList_DEFINED
#define SkRasterPipelineOpList_DEFINED

// `SK_RASTER_PIPELINE_OPS_LOWP` defines ops that have parallel lowp and highp implementations.
#define SK_RASTER_PIPELINE_OPS_LOWP(M)                                \
    M(move_src_dst) M(move_dst_src) M(swap_src_dst)                   \
    M(clamp_01) M(clamp_a_01) M(clamp_gamut)                          \
    M(premul) M(premul_dst)                                           \
    M(force_opaque) M(force_opaque_dst)                               \
    M(set_rgb) M(swap_rb) M(swap_rb_dst)                              \
    M(black_color) M(white_color)                                     \
    M(uniform_color) M(uniform_color_dst)                             \
    M(seed_shader)                                                    \
    M(load_a8)     M(load_a8_dst)   M(store_a8)    M(gather_a8)       \
    M(load_565)    M(load_565_dst)  M(store_565)   M(gather_565)      \
    M(load_4444)   M(load_4444_dst) M(store_4444)  M(gather_4444)     \
    M(load_8888)   M(load_8888_dst) M(store_8888)  M(gather_8888)     \
    M(load_rg88)   M(load_rg88_dst) M(store_rg88)  M(gather_rg88)     \
    M(store_r8)                                                       \
    M(alpha_to_gray) M(alpha_to_gray_dst)                             \
    M(alpha_to_red) M(alpha_to_red_dst)                               \
    M(bt709_luminance_or_luma_to_alpha)                               \
    M(bt709_luminance_or_luma_to_rgb)                                 \
    M(bilerp_clamp_8888)                                              \
    M(load_src) M(store_src) M(store_src_a)                           \
    M(load_dst) M(store_dst)                                          \
    M(scale_u8) M(scale_565) M(scale_1_float) M(scale_native)         \
    M( lerp_u8) M( lerp_565) M( lerp_1_float) M(lerp_native)          \
    M(dstatop) M(dstin) M(dstout) M(dstover)                          \
    M(srcatop) M(srcin) M(srcout) M(srcover)                          \
    M(clear) M(modulate) M(multiply) M(plus_) M(screen) M(xor_)       \
    M(darken) M(difference)                                           \
    M(exclusion) M(hardlight) M(lighten) M(overlay)                   \
    M(srcover_rgba_8888)                                              \
    M(matrix_translate) M(matrix_scale_translate)                     \
    M(matrix_2x3)                                                     \
    M(matrix_perspective)                                             \
    M(decal_x)    M(decal_y)   M(decal_x_and_y)                       \
    M(check_decal_mask)                                               \
    M(clamp_x_1) M(mirror_x_1) M(repeat_x_1)                          \
    M(clamp_x_and_y)                                                  \
    M(evenly_spaced_gradient)                                         \
    M(gradient)                                                       \
    M(evenly_spaced_2_stop_gradient)                                  \
    M(xy_to_unit_angle)                                               \
    M(xy_to_radius)                                                   \
    M(emboss)                                                         \
    M(swizzle)                                                        \
    M(debug_x) M(debug_y) M(debug_r) M(debug_g) M(debug_b) M(debug_a) \
    M(debug_r_255) M(debug_g_255) M(debug_b_255) M(debug_a_255)

// Operations that require floating-point processing.
#define SK_RASTER_PIPELINE_OPS_HIGHP_ONLY(M)                                                  \
    M(callback)                                                                               \
    M(unbounded_set_rgb)  M(unbounded_uniform_color)                                          \
    M(unpremul)           M(unpremul_polar)      M(dither)                                    \
    M(load_16161616)      M(load_16161616_dst)   M(store_16161616)       M(gather_16161616)   \
    M(load_a16)           M(load_a16_dst)        M(store_a16)            M(gather_a16)        \
    M(load_r16)           M(load_r16_dst)        M(store_r16)            M(gather_r16)        \
    M(load_rg1616)        M(load_rg1616_dst)     M(store_rg1616)         M(gather_rg1616)     \
    M(load_f16)           M(load_f16_dst)        M(store_f16)            M(gather_f16)        \
    M(load_af16)          M(load_af16_dst)       M(store_af16)           M(gather_af16)       \
    M(load_rf16)          M(load_rf16_dst)       M(store_rf16)           M(gather_rf16)       \
    M(load_rgf16)         M(load_rgf16_dst)      M(store_rgf16)          M(gather_rgf16)      \
    M(load_f32)           M(load_f32_dst)        M(store_f32)            M(gather_f32)        \
    M(load_1010102)       M(load_1010102_dst)    M(store_1010102)        M(gather_1010102)    \
    M(load_1010102_xr)    M(load_1010102_xr_dst) M(store_1010102_xr)     M(gather_1010102_xr) \
    M(load_10x6)          M(load_10x6_dst)       M(store_10x6)           M(gather_10x6)       \
    M(gather_10101010_xr) M(load_10101010_xr)    M(load_10101010_xr_dst) M(store_10101010_xr) \
    M(store_src_rg)       M(load_src_rg)                                       \
    M(byte_tables)                                                             \
    M(colorburn) M(colordodge) M(softlight)                                    \
    M(hue) M(saturation) M(color) M(luminosity)                                \
    M(matrix_3x3) M(matrix_3x4) M(matrix_4x5) M(matrix_4x3)                    \
    M(parametric) M(gamma_) M(PQish) M(HLGish) M(HLGinvish) M(ootf)            \
    M(rgb_to_hsl) M(hsl_to_rgb)                                                \
    M(css_lab_to_xyz) M(css_oklab_to_linear_srgb)                              \
    M(css_oklab_gamut_map_to_linear_srgb)                                      \
    M(css_hcl_to_lab)                                                          \
    M(css_hsl_to_srgb) M(css_hwb_to_srgb)                                      \
    M(gauss_a_to_rgba)                                                         \
    M(mirror_x)   M(repeat_x)                                                  \
    M(mirror_y)   M(repeat_y)                                                  \
    M(negate_x)                                                                \
    M(bilerp_clamp_8888_force_highp)                                           \
    M(bicubic_clamp_8888)                                                      \
    M(bilinear_setup)                                                          \
    M(bilinear_nx) M(bilinear_px) M(bilinear_ny) M(bilinear_py)                \
    M(bicubic_setup)                                                           \
    M(bicubic_n3x) M(bicubic_n1x) M(bicubic_p1x) M(bicubic_p3x)                \
    M(bicubic_n3y) M(bicubic_n1y) M(bicubic_p1y) M(bicubic_p3y)                \
    M(accumulate)                                                              \
    M(perlin_noise)                                                            \
    M(mipmap_linear_init) M(mipmap_linear_update) M(mipmap_linear_finish)      \
    M(xy_to_2pt_conical_strip)                                                 \
    M(xy_to_2pt_conical_focal_on_circle)                                       \
    M(xy_to_2pt_conical_well_behaved)                                          \
    M(xy_to_2pt_conical_smaller)                                               \
    M(xy_to_2pt_conical_greater)                                               \
    M(alter_2pt_conical_compensate_focal)                                      \
    M(alter_2pt_conical_unswap)                                                \
    M(mask_2pt_conical_nan)                                                    \
    M(mask_2pt_conical_degenerates) M(apply_vector_mask)

// The combined set of all RasterPipeline ops:
#define SK_RASTER_PIPELINE_OPS_ALL(M)    \
    SK_RASTER_PIPELINE_OPS_LOWP(M)       \
    SK_RASTER_PIPELINE_OPS_HIGHP_ONLY(M)

// An enumeration of every RasterPipeline op:
enum class SkRasterPipelineOp {
#define M(op) op,
    SK_RASTER_PIPELINE_OPS_ALL(M)
#undef M
};

// A count of raster pipeline ops:
#define M(st) +1
    static constexpr int kNumRasterPipelineLowpOps  = SK_RASTER_PIPELINE_OPS_LOWP(M);
    static constexpr int kNumRasterPipelineHighpOps = SK_RASTER_PIPELINE_OPS_ALL(M);
#undef M

#endif  // SkRasterPipelineOpList_DEFINED
