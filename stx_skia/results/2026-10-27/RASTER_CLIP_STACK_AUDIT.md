# Raster clip stack audit — 2026-10-27

Scope (relative to `skia/`): `src/core/SkRasterClipStack.h`, the clip stack that
`SkBitmapDevice` uses for every CPU canvas, and the shader-clip entry of
`src/core/SkRasterClip.cpp` that it forwards to. The header was converted on 2026-10-26
(c4ce909e, ffbc4c49; REGION_CLIP_CONVERSION.md and RASTER_CLIP_REPLACEMENT.md). The AST
inventory and the lexical checker find nothing in it, so this audit looks at behaviour. There are
two commits:
- 9b2eba92: a sequence corpus, with no library change;
- the fix commit that adds this record.

## New corpus (9b2eba92)

`tests/raster_clip_stack.cpp` now also writes `raster-clip-stack.raw`, which CI compares. It
runs 40 random sequences of 160 operations each, covering:
- saves and matching restores;
- rect, rounded-rect, oval and triangle clips under identity, translation, scale, rotation and
  skew, with both clip operations, antialiased and not;
- region clips and shader clips;
- stacks of 64x48, 1x1, 0x0, 0x9 and 20000x8, the last wide enough to disable antialiased
  clipping.

Each step records:
- the save depth;
- the clip's empty, rect, AA and complex flags, and its bounds;
- whether a non-empty clip has a shader;
- its region rectangles or AA bounds.

Each run then unwinds to the root. Random draws are taken into named locals, because C++ leaves
the evaluation order of function arguments unspecified.

**The corpus at first differed from the reference, before any change in this audit.** Every
differing record was in the shader-presence flag, and two earlier, intended candidate behaviours
explain them all:
- Replacing a clip drops its shader clip (the 2026-10-26 replacement fix). The reference keeps
  the stale shader, so later non-empty clips still carry it: 224 records.
- An emptied clip drops its shader. An empty clip draws nothing either way: 2,765 records.

So the shared corpus records shader presence only for non-empty clips, and sequences that also
replace the clip go to `raster-clip-stack-replaced.raw`, which is meant to differ. There, all
224 differing records are non-empty clips that have no shader in the candidate and a stale one
in the reference. `raster-clip-stack.raw` (6,440 cases, 247,093 bytes) matches the reference
byte for byte.

## Defects fixed

All four were unreachable through `SkCanvas`, which pairs device saves and restores and filters
null shaders. They were reachable through the stack's and device's own interfaces, which put
the precondition only in Debug checks or not at all.

1. **An unmatched restore emptied the stack in Release.** `restore()` popped the root record
   when nothing was saved; only a Debug assertion and a Debug-only counter caught it. Every
   later `rc()` or clip then read past the empty block list. The precondition is now a Release
   check. The Debug-only counter, which duplicated it and gave the class a different layout in
   Debug builds, is removed.
2. **The deferred save count could overflow.** Saves that do not change the clip only
   increment an `int`, so 2^31 of them would be signed overflow. The count is now Release-checked.
3. **The constructor accepted negative sizes that `setNewSize` refuses.** Both now Release-check
   them.
4. **A null shader clip erased an earlier shader clip.** `SkRasterClip::op(sk_sp<SkShader>)`
   composed the new shader with the existing one through `SkShaders::Blend`. Blend returns null
   when either input is null, so clipping by a null shader removed the earlier shader clip: the
   clip grew. A null shader now leaves the clip unchanged. Deferred saves and their restores
   keep the shader, as before.

`replaceClip` now also runs the stack's Debug validation, like every other clip entry.

## Death tests

The test already had abort modes for the two `setNewSize` checks, but nothing ever ran them.
`cmake/expect_stop.cmake` runs a test program in one mode and passes only if the process is
stopped, by a trap or an abort; any normal exit fails. CTest now runs four such tests on the
candidate:
- a resize with a save pending;
- a negative resize;
- a negative construction;
- an unmatched restore.

The overflow check has no death test, because reaching it takes 2^31 saves.

Abort modes in about a dozen other tests (arena, block list, deque, mask, mipmap, pixel
storage, read buffer, and others) are still not registered. That is outside this audit.

## Verification

- `null_shader_keeps_clip_case` (candidate only) checks that a null shader keeps an earlier
  shader clip, adds none to a plain clip, and survives deferred saves and restores.
- Both clip-stack corpora are byte-identical to the corpus commit's own output, so the fixes
  change no recorded behaviour; the corpora contain no null shaders and no unmatched restores.

**Gates.** All were run sequentially at the fix commit:
- Release candidate: 82/82, including the four death tests;
- reference: 77/77;
- all shared artifacts match, except the intentional `image-sampling-corrected.raw`,
  `m44-corrected.raw`, `matrix-invert-small.raw` and `raster-clip-stack-replaced.raw`;
- instrumented suite: PASS.

**Runner check.** Run by hand against the candidate build:
- an unknown mode, where the program runs normally and exits with status 0, fails the runner;
- `--unmatched-restore` passes it, reported as `SIGTRAP` from the Release check's trap.
