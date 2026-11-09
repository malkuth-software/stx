# Raster pipeline defect fixes — 2026-10-27

These fixes follow the mechanical step in RASTER_PIPELINE_SPELLING.md (49eab37). Each is a
separate change.

## 1. Memory contexts arrive mutable

**Defect.** `appendLoad`, `appendLoadDst` and `appendStore` took
`const SkRasterPipelineContexts::MemoryCtx*`. `append(op, const void*)` cast away const for any
op. But `run()` and compiled thunks write every recorded memory context: for a row's tail, they
repoint `pixels` at biased scratch storage, run the program, and restore the pointer. Writing a
context that is actually a const object is undefined behaviour. The signatures also told
callers their context was left alone. The raster blitter relied on this. Its `const` helpers
`appendLoadDst` and `appendStore` handed out the blitter's own `fDstPtr`, which its compiled
blits then repointed on every tail.

**Fix.**
- The three helpers take a mutable `MemoryCtx*`.
- The op table that records memory use is now one named function, `memory_use()`; it was a
  switch inside `uncheckedAppend`. `uncheckedAppend` and the const overload both use it.
- `append(op, const void*)` now refuses, in Release, every op whose context `run()` patches: the
  loads, stores, scale and lerp memory ops, `srcover_rgba_8888`, the debug stores, and
  `emboss`'s context pair. Read-only contexts (constants, matrices, transfer functions) still
  pass through it.
- The blitter's `appendLoadDst` and `appendStore` are no longer `const`.
- No other caller changed: each already passed a mutable local or member.

**Evidence.**
- Compile-time checks in `tests/raster_pipeline_corpus.cpp` (candidate only) accept mutable
  contexts and reject const ones for all three helpers.
- The Release refusal has no death test. The suites exercise every retained caller without
  reaching it.
- Rendering is unchanged:
  - Candidate release 75/75 PASS; reference release 74/74 PASS.
  - The pipeline corpus (hash 13484477241382582955) and every other shared artifact match;
    `image-sampling-corrected.raw` differs, an earlier intentional correction.
  - Instrumented suite PASS; diagnostics unread.
- The kernel header is untouched, so the object-code identity check in the mechanical step
  still covers it.

## 2. Each op's context type is declared and checked on both sides

**Defect.** The kernel header's `Ctx` adapter converted a stage's `void*` context, with a
C-style cast, to whatever pointer type the stage declared. `append()` took `void*` (or
`const void*`) for every op. Nothing tied the pointer a caller appended to the type its stages
read. Checking every call site against stage declarations found 33 type puns among 185 typed
appends:
- `SkImageShader` passed its gather contexts through `void*` (28 sites).
- The raster blitter held its clip-coverage buffer as `void*`.
- `SkTriColorShader` passed its live `SkMatrix` member to `matrix_perspective`, which read it
  as nine floats through `SkMatrix`'s private layout, and passed a `Matrix43` struct for
  `matrix_4x3`.
- `SkConicalGradient` passed a pointer to its whole `uint32_t[16]` mask array.
- `gamma_` read a `skcms_TransferFunction` through a float pointer to its first field.
- Five lane-storage ops (`load_src`, `store_src`, `store_src_a`, `load_dst`, `store_dst`) and
  `scale_native`/`lerp_native` read the same buffer as `float` in highp and as `uint16_t` in
  lowp, with nothing saying so.

**Fix.**
- **The table.** New `src/core/SkRasterPipelineOpContextTypes.h` gives every op (232) an
  explicit specialization of `SkRasterPipelineOpContext`. Each says what `append()` accepts and
  what each precision's stage receives. The kinds are:
  - none (67 ops);
  - an immediate value (`swizzle`);
  - a pointer to an object (157 ops), const when read-only and mutable when stages write it or
    `run()` repoints it as a memory context;
  - lane storage (7 ops), float for highp and `uint16_t` for lowp, now stated explicitly.

  The entries were derived from the stage declarations. Where the declarations disagreed with
  the callers, the code was fixed, not the table.
- **Stages.** Each stage-defining macro (`DECLARE_HIGHP_STAGE` and the six `LOWP_STAGE_*`
  forms) asserts at compile time that the kernel's declared context parameter matches the
  entry for its op and precision. The parameter must point to the entry's object; it may add
  const but not drop it. The untyped `Ctx` is replaced by `StageContext<op, precision>`, which
  converts only to the entry's pointer type. A missing entry, or a stage declaring another
  type, fails to compile.
- **Appends.** `append(op)`, `append(op, Object*)` and `append(op, uintptr_t)` check the
  argument against a constexpr table built from the entries, in Release. The checks cover kind,
  object type, and mutability where the entry requires it. Passing `void*` fails to compile.
  The internal helper appends (constant colors, set-RGB, transfer functions, clamps) check the
  same way. The const-overload refusal from fix 1 is now one case of this check. A new
  `appendRecordedStage()` re-appends a stage another pipeline recorded; the blitter's debug
  path now uses it instead of re-appending untyped contexts.
- **Callers fixed:**
  - `SkImageShader`'s gather contexts are typed.
  - The blitter's clip buffer is declared as the float lane storage it is.
  - `SkConicalGradient` passes the mask array itself.
  - `SkTriColorShader` keeps a row-major float copy of its 3x3 matrix, refreshed in
    `update()`, and passes `Matrix43`'s coefficient array.
  - `gamma_` takes the transfer function and reads `g` by name.

**Evidence.**
- **Call-site inventory.** A build that temporarily marked the `append` template deprecated
  listed all 185 typed call sites with their deduced types. After the fixes, every
  constant-op site matches its entry. Four sites choose the op at run time; they get the
  Release check.
- **Compile-time tests** (candidate only, in `tests/raster_pipeline_corpus.cpp`) exercise the
  stage rule: matching, const-adding, const-dropping, wrong-object, none, immediate and
  lane-storage cases.
- **Object-code identity:** the kernel header's `SkOpts.cpp` compiles to identical code,
  function by function, with only absolute branch addresses masked. This holds on arm64 NEON
  and scalar and on x86-64 SSE2, SSE4.1, AVX, AVX2, AVX-512 and scalar, with equal section
  sizes. Getting there took one form change, recorded in the header:
  - helper functions returning the context changed register allocation in `perlin_noise` and
    `bicubic_clamp_8888` (tried as static, forced and inline helpers, and with or without the
    kernel argument);
  - a temporary that converts at the kernel call, the adapter's original form, compiles to
    the same code.
  LoongArch and 32-bit ARM were not compiled here.
- **Rendering:**
  - Candidate release 75/75 PASS; reference release 74/74 PASS.
  - The pipeline corpus (hash 13484477241382582955), the canvas corpus (whose meshes draw
    colored vertices under perspective, exercising the tri-color fix) and every other shared
    artifact match. `image-sampling-corrected.raw` differs, an earlier intentional correction.
  - Instrumented suite PASS; diagnostics unread.
- **House policy:** no new AST findings, and the whole-library count stays 2,076. The
  lexical checker reports only the known 32-bit-ARM lambda.

**Cost.**
- Each append now does one table lookup and up to four comparisons. Appends happen while
  pipelines are built, not per pixel.
- `SkTriColorShader::update()` copies nine floats per triangle.
- Neither cost was measured.

## Still open

- **Shared state in compiled pipelines.** A compiled pipeline's program and patch records, and
  the contexts they repoint, are shared by every copy of the thunk. Overlapping invocations
  race. `compile()` documents this. Callers today are single-threaded blitters that own their
  thunks.
- **Run-time ops.** Four call sites choose the op at run time: the blitter's debug panels,
  image-shader sampler setup, and transform-shader matrices. They are checked in Release, not
  at compile time.
