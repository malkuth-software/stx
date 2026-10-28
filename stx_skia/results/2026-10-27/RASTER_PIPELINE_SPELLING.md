# Raster pipeline: construction conversion and kernel respelling — 2026-10-27

Scope (relative to `skia/`): `src/core/SkRasterPipeline.cpp/.h`, and the kernel header
`src/opts/SkRasterPipeline_opts.h`. All three already had original reference copies under
`baseline/skia/`, and the reference profile selects them. This record covers the mechanical
step only. The defects it found are listed at the end and fixed in separate changes.

## Pipeline construction (`SkRasterPipeline.cpp/.h`)

- The `COLOR_TYPE_CASE` macro becomes explicit cases. Each load, destination load and store
  records its memory context's pixel size. Member arrows become `(*p).m`, `auto` is spelled
  out, and C-style casts become `static_cast`. `this->` qualifiers and the shadowing
  `stagesNeeded` locals are gone.
- `extend()` copies the source stage list with a counted loop instead of `while (n --> 1)`.
- The memory-context search's `find_if` lambda becomes a loop over the records. The first match
  still widens its load and store flags.
- `compile()` returns a named functor (`CompiledPipeline`) through `std::function`, and an
  empty pipeline returns `EmptyCompiledPipeline`. The comments state that the functor borrows
  its program and patches from the pipeline's arena, and that invocations must not overlap.
- Members, the stage-list record and `gForceHighPrecisionRasterPipeline` start initialized. The
  program stage `SkRasterPipelineStage` deliberately stays trivially constructible: `run()`
  assembles programs in uninitialized storage, and `AutoSTMalloc` requires that.
- The X-macro op-name switch remains. It is the op list's own interface.
- **Correction:** both program builders now check each op index against the op table in Release
  before indexing `SkOpts::ops_lowp`/`ops_highp`. Before, a negative index was unchecked in lowp,
  and highp had no check at all.

## Kernel header (`SkRasterPipeline_opts.h`)

- **Pointer arrows.** 247 came from the GUI.Forms AST tool's plans, merged across eight ISA
  configurations. The stage macros' `params->`, the LoongArch gradient blocks' `c->` and one
  Debug assertion were converted by checked patterns.
- **`auto`.** 178 sites are spelled from the AST tool's per-ISA deduced types, mapped to the
  source's lane aliases (`F`, `I32`, `U32`, `U16`, `U8`, `U64`), to pointer types, or to
  ISA-local intrinsic types. Each site resolved to one spelling valid on every configuration.
  Comparison masks are spelled `I32`. On the scalar path that holds 0 or 1 where `bool` did,
  and every use converts it to `I32` anyway. Stage-macro `auto`s become `F` and `Stage`, and
  the `Stage` C-style casts become `reinterpret_cast`.
- **Lambdas.** Twelve become named functions:
  - the HSL hue step and five transfer-function channels;
  - the AVX2/LASX/default `from_8888` narrowing;
  - `twice`, `round_color_to_U16`, and the two bilinear lerp steps.
  `gamma_channel` and the bilerp steps keep ordinary `inline` instead of `SI`'s
  `always_inline`. Forcing inlining reordered `gamma_`'s loads and cost NEON
  `bilerp_clamp_8888` a callee-saved register spill.
- One two-name pointer declaration is split, because a spelled pointer type applies only to its
  first declarator.

### Object-code identity

`SkOpts.cpp` is the header's only shipping translation unit. It was compiled with the
release flags before and after, for eight configurations: arm64 NEON, arm64 scalar, and
x86-64 SSE2, SSE4.1, AVX, AVX2 (x86-64-v3), AVX-512 (x86-64-v4) and scalar. Disassembly with
relocations was compared function by function, after removing absolute addresses.

- The final source has no differing function in any configuration: 346 functions on SIMD
  configurations and 236 on scalar ones. Section sizes are equal.
- This includes the default-x86 and NEON code that the candidate actually ships.

**Not compiled here:** LoongArch LSX/LASX and 32-bit ARM. Clang has no Linux sysroot on the
M4. The LoongArch edits there are pointer arrows on one context and 14 `auto`s; each LSX
intrinsic's return type was checked in clang's `lsxintrin.h`. The 32-bit-ARM-only `sqrt`
lambda in lowp `sqrt_` is left as it is.

### What remains in the kernel header

The AST tool reports no findings. The lexical checker reports two: the 32-bit-ARM lambda and its
`auto`. Outside both checkers remain:
- 44 macro definitions, including the stage-generating `HIGHP_STAGE`/`LOWP_STAGE_*` machinery
  behind 313 stage definitions;
- about 280 C-style casts, mostly vector reinterpretations;
- about 112 multi-name declaration lines;
- three `typedef`s.
These need design changes, not respelling.

## Verification

- **New corpus:** `tests/raster_pipeline_corpus.cpp` writes `raster-pipeline.raw`, which CI
  compares. It covers:
  - a fixed list of retained op names;
  - for every color type: load, destination load, source-over and store, at four origins and
    six widths (tail patching included), run directly and compiled, in lowp and forced highp;
  - constant colors and set-RGB, in and out of range;
  - every matrix stage class;
  - all five transfer-function stages and sRGB loads and stores;
  - extension with a shared memory context whose load and store use merges;
  - the emboss stage's two contexts.
  The candidate also checks that every op index has a distinct name. Op indices differ between
  the builds, because the candidate removed compiler-only ops. 2,720 cases, 5,497,369 bytes,
  identical hash 13484477241382582955 in both builds.
- Candidate release: 75/75 PASS. Reference release: 74/74 PASS.
- Shared raw artifacts: 35 of 36 match byte for byte. `image-sampling-corrected.raw` differs,
  an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST findings in the three files. The whole-library count falls from 2,680
  to 2,076, in 99 files.

No timing claim is made. The kernels compile to the same code; construction adds two Release
comparisons per stage when a program is built.

## Defects found, fixed separately

1. **Const memory contexts are written.** `appendLoad`, `appendLoadDst` and `appendStore` take
   `const MemoryCtx*`, and `append(op, const void*)` casts away const. Yet `run()` replaces each
   recorded context's `pixels` pointer for the row tail, and restores it afterwards. A context
   that is actually `const` is undefined behaviour. Any reader that shares the context during
   the run sees the scratch address.
2. **Concurrent invocations of one compiled pipeline race.** Its program shares those contexts
   and patch records. This is now documented, not prevented.
3. **The `Ctx` adapter converts to any pointer type.** It implicitly C-style-casts the stage
   context to whatever the stage declared. A stage declared with the wrong context type compiles
   silently.
