# Working on stx_skia in GUI.Forms

This note began as the 2026-10-27 handoff from the `malkuth-software/stx_skia` repository
(then `falseywinchnet/stx_skia`, PR #1, branch `codex/cpu-library`) to the GUI.Forms sessions.
The library now lives in GUI.Forms as `stx_skia/`; the repository is the read-only record of the
intake and holds the reference sources. Read AGENTS.md, docs/INTAKE.md, docs/PLAN.md and
`planning/PROGRAMMING_HOUSE_STYLE.md` at the GUI.Forms root first. Then read this note,
docs/CONVERSION.md (the ledger; its "Still required" section lists open work) and
docs/OPTIMIZATION_STUDY.md (before any performance work).

At the pin, candidate 123/123 and reference 90/90 passed with no warnings, and each converted or
fixed unit has a record in `results/`.

## Owner decisions to respect

- **Fix, don't just respell.** "Conversion" means fixing or replacing bad code, not respelling
  it.
- **Prune lightly.** Keep general-purpose APIs whole (`TArray` and the like); ask before removing
  members. Whole unused internal subsystems may go, with build proof. Public APIs with no caller
  (`SkDataTable`, `SkPoint3`, `SkTiledImageUtils`, the Perlin-noise shaders, `SkTime`,
  `SkFloatInterpFunc`, `SkMaskFilter::Deserialize`) stay and are converted with their units.
- **Android pieces stay.** The owner may use them.
- **No GPU backend.** Codecs and font backends stay outside the library.
- **Performance rules.** No fast-math, no guessed `restrict`, no unmeasured SIMD claims. New SIMD
  targets 128-bit portable kernels. Imported optimized code is the baseline; measure with the
  GUI.Forms renderer A/B (docs/OPTIMIZATION_STUDY.md).
- **Ordinary GUI.Forms code.** The instrumented suite's diagnostics are visible like any other
  test output, a memory error or undefined behaviour is a defect, and the GUI.Forms checkers
  and reviews cover this directory.

## How a unit is done

1. **Find users.** Run `tools/link_closure.py` (symbol level, compiled objects only) and grep
   the GUI.Forms sources that call the unit.
2. **Reference.** The reference profile compiles the library from the pinned repository, so a
   change here is always compared with the file as it was pinned; no intake copy is needed. Where
   the pin already holds a converted file, its `baseline/` copy (listed in
   `cmake/reference.cmake`) is the intake original.
3. **Corpus.** Write a `tests/<unit>_core.cpp` on `tests/differential_corpus.h`. Register it in
   `CMakeLists.txt` (the executable and test, the `STX_SKIA_REFERENCE=1` definition and the
   `-fno-rtti` options) and in `tests/corpora.txt`, which `tools/build_native.sh` runs and
   `tools/compare_profiles.py` compares.

   Rules for what the corpus records:
   - Never record addresses.
   - Keep alignments at 8 or below. `alignof(std::max_align_t)` is 8 on Apple arm64, MSVC and
     wasm, so 16-byte alignment is address-dependent there.
   - Use only inputs both builds define. Put intentional differences in a second artifact
     passed as `argv[2]`, list it in `tests/intentional_differences.txt`, and show that every
     difference has the stated cause.
   - Commit the corpus with both builds identical.
4. **Respell.** Make a mechanical respelling, prove the artifacts byte-identical to the
   pre-rewrite output and to the reference, lower the file's entry in
   `house_style_allowance.txt`, and commit.
5. **Fix.** Make the fixes, with candidate-only checks under `#ifndef STX_SKIA_REFERENCE` and
   Release-check stop tests. Stop tests are registered with `cmake/expect_stop.cmake`, which
   passes only on the trap a Release check raises.
6. **Negative control.** Run the new checks against the unfixed sources in a fresh build tree
   (or after `cmake --build <tree> --clean-first`): a restored file with an older time stamp
   leaves objects ninja won't rebuild.
7. **Close out.** Run candidate, baseline and candidate-sanitize, compare the profiles, run the
   installed check (`tools/verify_installed.sh` on fresh `build/install-*` directories) and the
   GUI.Forms Skia tests, and write a record in `results/` and a ledger entry.

Commands, from this directory with the GUI.Forms LLVM toolchain on the path:

```sh
sh tools/build_native.sh candidate
STX_SKIA_REFERENCE_ROOT=<stx_skia checkout at the pin> sh tools/build_native.sh baseline
python3 tools/compare_profiles.py build/baseline build/candidate
sh tools/build_native.sh candidate-sanitize
python3 ../tools/check_stx_skia_style.py
```

## Intentional differences between the profiles

`tests/intentional_differences.txt` lists them (nine at the pin); each unit's record in
`results/` explains its entry in full.

## Open work, roughly in order

1. **Support leftovers.**
   - **Code:** `SkRandom`, `SkChecksum`, CPU detection (`SkCpu`), time, debug, assertion and
     log helpers, the base headers (`SkTypes.h`, `SkFeatures.h`, `SkMacros.h`,
     `SkAttributes.h`). The OS file ports and `SkData`'s file loaders are removed (API item 4).
   - **Compatibility remnants:** the `SkSpan` traits (`sk_sp::operator->` is removed).
2. **Colour.** `SkColor`, `SkColorSpace`, `SkBlendMode` and `SkColorData`. Ask the owner whether
   skcms (about 5,600 lines of third-party colour management) is converted or kept as an
   imported module before starting on it.
3. **Pixel kernels.** `SkVx`, `Sk4px`, row blending, mask blitting and swizzles. These are the
   optimized baseline: give them corpora and a respelling, and change an algorithm only with
   measurement. The wasm build (SSE4.1 over SIMD128) and the planned Android NDK build (NEON)
   must keep compiling.
4. **Canvas-adjacent code.** `SkRSXform`, `SkPoint3`, lattice iteration, `SkTiledImageUtils`,
   `SkNoDrawCanvas`, `SkPathUtils`, and the out-of-line `SkCanvasPriv` lattice helpers (unused).
5. **Deferred defects.** The never-released CPU recorder and context singletons;
   `SkMatrix::normalizePerspective` with infinite entries; about a dozen unregistered stop
   modes.
6. **Swap kernel follow-ups on the GUI.Forms side:** the byte loops that `stx_skia::swizzle`
   replaces, and wasm object-code inspection. (`release_hdc`'s missing swap for RGBA bitmaps is
   fixed in GUI.Forms.)
7. **Build profiles.** An Android NDK build (aarch64, and x86_64 for the emulator), and a check
   that GUI.Forms' wasm configuration still compiles.
8. **Optimization.** Follow docs/OPTIMIZATION_STUDY.md. Fills are near store bandwidth, so the
   open lever is overdraw.
9. **Close out.** A full audit of every retained header and source list, then the owner's
   review.

## Pitfalls already met

- **Shell.** zsh doesn't word-split variables and treats `$P:s` as a modifier. Run loops under
  `bash -c` and write `${P}:path`.
- **Stale objects.** `tools/link_closure.py` counts only objects of sources in
  `compile_commands.json`; objects left in the build directory misled one earlier check.
- **`SkUNREACHABLE` traps.** It is `__builtin_trap()` (or `__fastfail` with MSVC), so bounds and
  emptiness checks built on it already stop in Release.
- **`SK_MinS32` is `-INT32_MAX`,** not `INT32_MIN`. The saturating helpers clamp there, so don't
  reuse them where `INT32_MIN` is a valid result.
