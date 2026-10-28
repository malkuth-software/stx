# Raster kernels: casts, declarations and stage macros — 2026-10-27

This covers `src/opts/SkRasterPipeline_opts.h` (relative to `skia/`) and the files that consume
its stages. There are three commits, each verified on its own: fbfad11 (casts), 112c80c
(declarations) and the stage-macro commit that adds this record.

## Method

`SkOpts.cpp` is the header's shipping translation unit. Each step compiles it with the release
flags for eight configurations: arm64 NEON and scalar, and x86-64 SSE2, SSE4.1, AVX, AVX2
(x86-64-v3), AVX-512 (x86-64-v4) and scalar. The objects are then compared function by function.

- Absolute branch addresses and page labels are masked.
- After the macro step, function order and names change. `obj_compare.py` keys each stage
  template instance (`highp_stage<(SkRasterPipelineOp)N, …>`, `lowp_*_stage<…>`) by its op, as
  the macro-generated stages were named. Branch targets are written relative to their own
  function. Each x86-64 RIP-relative operand is replaced by the 16 bytes it addresses in the
  object, so a constant load compares by the constant it reads, not by its position. A
  self-comparison of every object reports no difference.
- The dispatch tables are checked slot by slot through their data relocations, and section
  sizes and the absence of static initializers are compared.
- LoongArch and 32-bit ARM cannot be compiled here, for lack of a Linux sysroot. Their
  ISA-only blocks keep their casts and multi-name declarations.

## 1. C-style casts (fbfad11)

clang-query listed every written C-style cast on each configuration, with its effective
conversion kind (the cast and its `part_of_explicit_cast` children). In vector code a C-style
cast reinterprets bits; on the scalar path the same spelling converts values. 403 sites were
replaced:
- Vector and same-size reinterpretations (301) become `bit_cast_lanes<T>(v)`, a by-value
  `__builtin_bit_cast`. This includes 11 sites that are bit casts on vector paths and
  same-size integer conversions on the scalar path, and 12 whose operands are intrinsic macros.
- Pointer views (79) become `reinterpret_cast`.
- Value conversions, and `void*` to `T*`, become `static_cast` (23).

The memcpy-based `sk_bit_cast` and the reference-taking `std::bit_cast` were measured and
rejected. Both changed scheduling and register allocation in five AVX2 and three AVX-512
functions. The by-value helper removes all of that except one case.

**Result.** Seven configurations are identical. On AVX2, `load_f32`, `load_f32_dst` and
`callback` hold the same instructions, with one independent address computation scheduled
elsewhere. AVX2 is not a shipped candidate configuration: x86-64 builds use the SSE2 baseline
(`STX_SKIA_BASELINE_SIMD`).

## 2. Multi-name declarations (112c80c)

130 declaration statements with more than one declarator were found through the AST and split
in their original order. Comments stay with their declarators, and a reference declarator
keeps its `&` (`F& t = r;`). The objects are identical to the previous step on all eight
configurations.

## 3. Stage-generating macros

- **Kernels.** `DECLARE_HIGHP_STAGE`, `HIGHP_STAGE`, the six `LOWP_STAGE_*` forms and the four
  `BLEND_MODE` macros are gone. Each of the 209 highp and 89 lowp stages is now an ordinary
  `SI void name_k(context, dx, dy, registers…)` kernel, with the signature the macro generated
  written out.
- **Blend modes.** The 23 highp and 20 lowp blend modes are `SI` channel functions (lowp has
  two sets, selected by `SK_USE_INACCURATE_DIV255_IN_BLEND`). They run through
  `blend_each_channel_k<channel>` or `blend_colors_k<channel>` in each precision.
- **Wrappers.**
  - Highp: `highp_stage<op, kernel>`.
  - Lowp: `lowp_geometry_stage`, `lowp_gather_stage` and `lowp_pixel_stage`.
  - Each has a narrow and a wide calling convention. The `SKRP_NARROW_STAGES` choice is still
    made by the preprocessor, which is configuration rather than code generation.
  - Each wrapper takes its kernel as a pointer whose context parameter is exactly the op's
    `SkRasterPipelineOpContext` entry for that precision. A kernel with another context type
    does not compile, which replaces the per-stage assertions.
- **Tables.**
  - `kHighpStages` (232 entries) and `kLowpStages` (109 entries) list each op's stage
    explicitly, and a `consteval` check enforces op order.
  - The scalar build's null lowp table is a `consteval` array instead of an X-macro.
  - `SkOpts.cpp` indexes the tables by op value. `SkOpts_ml3/ml4/lasx.cpp` copy them in loops.
    Their originals are not in the candidate build; `SkOpts_ml3.cpp`'s intake copy now
    serves the reference profile.
- **Exact table types.** The context table lists each precision's exact stage type. 71 memory
  contexts are appended mutable and read through const. Four kernels declared mutable contexts
  they only read: `perlin_noise`, `mipmap_linear_finish`, `load_src_rg` (a load declared
  `float*`) and lowp `clamp_x_and_y`. They now take const contexts.
- **Remaining macros.** 30 of 44 remain. They are ISA selection, `ABI`, `SI`, `SK_UNROLL`, the
  `SKRP_SUPPORTS_OPTIMIZED_DIV_*` flags, the include guard, and the LoongArch-only
  `_LSX_TRANSPOSE4_S`.

**Result.**
- All eight configurations are identical function by function: 346 functions on SIMD
  configurations and 236 on scalar ones.
- The dispatch tables match slot by slot. Section sizes are equal, and no static initializer
  was added.
- The narrow-stage wrappers, which no tested configuration selects, were compiled by forcing
  narrow stages in scratch copies of the old and new headers. Both arm64 and x86-64 are
  identical.
- The candidate `SkOpts_ml3.cpp` and `SkOpts_ml4.cpp` cross-compile for x86-64-v3 and v4.

## Verification

- Candidate release: 75/75 PASS. Reference release: 74/74 PASS.
- The pipeline corpus (hash 13484477241382582955), canvas, mask, blitter and every other shared
  artifact match. `image-sampling-corrected.raw` differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- Compile-time tests check the exact per-precision stage types, including the four constness
  fixes.
- House policy: the lexical checker reports only the 32-bit-ARM lambda, and the AST count stays
  2,076.

No timing claim is made; the shipped code is unchanged.
