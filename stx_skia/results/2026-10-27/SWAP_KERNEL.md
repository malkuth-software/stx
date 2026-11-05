# Standalone red/blue swap kernel — 2026-10-27

Implements `docs/SWAP_KERNEL_PLAN.md`, except its Windows and wasm steps. Raw numbers are in
`build/swizzle-arm64/` and `build/swizzle-x86_64/{candidate,baseline,v2}/`. Each holds
`measurements.json` and `measure.txt`, plus the generated assembly; this is generated
output, not tracked.

## What was added

- **`include/stx_skia/swizzle.hpp`.** It includes only standard headers.
  - **Unit types:** `PixelCount`, `RowCount` and `RowBytes` have explicit constructors and do
    not convert into each other or from integers.
  - **Stride conversion:** `row_bytes_for(PixelCount)` converts an Android-style pixel
    stride, with an overflow check.
  - **Results:** `SwizzleResult` names each failure.
  - **Calls:** `swap_red_blue(dst, src)` and `swap_red_blue_rows(dst, dstStride, src,
    srcStride, width, height)`.
  - **Byte order:** a `static_assert` requires a little-endian target, and is repeated in
    the source.
- **`src/swizzle.cpp`.** The kernel computes `(p & 0xFF00FF00) | rotl(p & 0x00FF00FF, 16)`
  per 32-bit word, moving words with `std::memcpy`.
  - **Two loops:** the in-place loop takes one address, so the compiler sees no possible
    overlap. The disjoint loop is chosen only after the overlap check.
  - **Validation first:** everything is checked before any write:
    - whole-pixel sizes;
    - matching sizes;
    - `width × 4`, `stride × (height − 1)` and the extent sum, each for overflow;
    - a stride narrower than a row when there is more than one row;
    - span lengths;
    - partial overlap. In place means the same start and the same stride; anything else that
      intersects is rejected.
  - **No hints:** no intrinsics, no target attributes, no `restrict` and no loop pragmas.
- **The `stx_skia_swizzle` library** (exported as `stx_skia::swizzle`). Its include path is
  only `include/`, so a Skia header in the kernel cannot compile. `stx_skia` links it
  publicly, and both are installed and exported.

## Correctness

`tests/swizzle.cpp` (`swizzle_contract`, both profiles) checks:
- **Against Skia:** the kernel is byte-identical to `SkSwapRB` and to an independent byte
  reference, in place and between separate buffers:
  - for 0–64 pixels and 255, 256, 1000, 1027 and 4099 pixels;
  - at source and destination byte offsets 0–3.
- **Round trip:** swapping twice restores the input, in every one of those cases. Strided
  rectangles in eight layouts also round-trip, in place and between buffers, with misaligned
  destinations and unaligned strides.
- **Sentinels:** padding before, between and after rows is never touched.
- **Failures:** every failure result leaves the destination unchanged:
  - narrow destination and source strides;
  - three overflow forms;
  - short destination and source spans;
  - non-whole-pixel sizes and mismatched sizes;
  - four partial overlaps: same start with different strides, offset start, start inside
    the last source row, and interleaved rows;
  - overlapping whole spans.
- **Successes:** zero width and zero height succeed without writing. One row needs no stride.
- **Stride conversion:** `row_bytes_for` is checked at its overflow boundary.
- **Types:** static assertions confirm the unit types don't convert.

On x86-64 the comparison was also run against Skia's AVX2 version, through the
reference build with `SkGraphics::Init`, and passed.

## Link isolation

- **The object file:** `llvm-nm -u swizzle.cpp.o` is empty. It defines only the three
  functions.
- **The isolated test:** `swizzle_without_skia` builds `tests/installed/swizzle_only.cpp`
  against `stx_skia_swizzle` alone.
  - **Headers:** it errors at compile time if Skia's include path is visible.
  - **Symbols:** the linked binary has no Skia symbol. Its only undefined names are the
    system's stack-guard symbols and, in the installed build, `memcmp`.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`. The
  full consumer calls both functions, and `swizzle_only` links `stx_skia::swizzle` alone.
  The earlier verification output on the mirror was moved to
  `build/previous-install-20261027/`, not deleted.

## Generated code

Every inspected build vectorizes at 128 bits, with a scalar tail:

| Build | Inner loop |
|---|---|
| arm64 (Homebrew clang 23.1.2, `-O3`) | `bic.8h` ×2, `rev32.8h`, `orr.16b` per vector, 16 pixels per iteration, then 4 |
| x86-64 SSE2 (preview from the M4, Apple target; and native clang 22.1.8 on Linux) | `pand`, `pslld`/`psrld`, `por` |
| x86-64 SSE4.1 / x86-64-v2 | `pand`, `pshufb` (the rotate), `por` |

Not inspected:
- **Windows:** the pinned MSYS2 CLANG64 toolchain. The Windows machine was not reachable.
  The Linux pod's clang has the same version (22.1.8) but a different target and runtime, so
  it stands in for the code-generation question only.
- **wasm32 SIMD128:** this machine has no wasm sysroot.

## Timing

**Method.** Per case and mode: one warm-up pass, then about 256 MiB of pixels in repeated
passes over the same buffers. Each figure is the median of seven process runs, in Gpx/s;
higher is faster.
- **Disjoint and in place:** "d" is separate buffers and "i" is in place.
- **Kernels:**
  - **stx:** this kernel;
  - **bytes:** the byte-permutation loop GUI.Forms has today;
  - **scalar:** that loop with vectorization disabled;
  - **skia:** `SkSwapRB` after `SkGraphics::Init`;
  - **skia default:** Skia's routine as compiled, before dispatch.
- **Cases:** 64 pixels; a 1920-pixel row; 300×200 with 4096-byte strides; 1920×1080;
  3840×2160.
- **Checks:** every kernel produced identical bytes per case and mode.

### arm64, M4 mini

Skia is its NEON version.

| Case | stx d / i | skia d / i | bytes d / i | scalar d / i |
|---|---|---|---|---|
| 64 px | 8.37 / 10.55 | 10.28 / 9.52 | 1.58 / 3.68 | 1.78 / 3.58 |
| row 1920 | 14.84 / 14.85 | 14.20 / 13.70 | 1.83 / 3.70 | 1.83 / 3.63 |
| 300×200 | 10.22 / 14.28 | 9.66 / 13.66 | 1.56 / 3.16 | 1.57 / 3.17 |
| 1920×1080 | 13.82 / 14.41 | 13.54 / 14.57 | 1.69 / 3.57 | 1.86 / 3.37 |
| 3840×2160 | 11.15 / 12.22 | 11.69 / 11.91 | 1.75 / 3.61 | 1.78 / 3.45 |

- **Against Skia's NEON version:** the kernel is level with it, from 0.95× to 1.08×, from one
  row up.
- **Loss at 64 pixels:** on separate buffers it is slower (0.81×, about 8 ns against
  6 ns).
- **The byte loop's fragility:**
  - In this build the compiler did not vectorize GUI.Forms' byte loop. It inlined it into the
    benchmark dispatcher as `ldrb`/`strb`.
  - In the first M4 build of the same source, before one kernel was added to the benchmark,
    the separate-buffer form did vectorize (13.8–15.0 Gpx/s). Its in-place form never did.
  - The byte form's vectorization depends on context. The mask form vectorized in every
    build.

### x86-64, Runpod AMD EPYC 9575F (Zen 5)

Built with Ubuntu clang 22.1.8, `-O3`, pinned to one CPU. The pod has two physical cores,
and the host load average was 80–100 from other tenants.

**Candidate profile, x86-64 baseline (SSE2).** This is what GUI.Forms ships. stx_skia's
baseline-SIMD policy disables Skia's run-time dispatch, so `skia` equals `skia default`.

| Case | stx d / i | skia (default) d / i | bytes d / i | scalar d / i |
|---|---|---|---|---|
| 64 px | 5.29 / 7.40 | 6.53 / 1.78 | 1.73 / 3.37 | 1.74 / 3.35 |
| row 1920 | 7.61 / 8.50 | 7.52 / 1.80 | 1.94 / 3.62 | 1.92 / 3.61 |
| 300×200 | 7.81 / 8.78 | 7.49 / 1.70 | 1.52 / 3.23 | 1.51 / 3.19 |
| 1920×1080 | 7.27 / 8.35 | 7.22 / 1.76 | 1.70 / 3.47 | 1.69 / 3.48 |
| 3840×2160 | 4.52 / 6.24 | 4.26 / 1.80 | 1.72 / 3.47 | 1.72 / 3.41 |

- **Against GUI.Forms' loops:** the kernel is 2.6–5.1× faster on separate buffers and
  1.8–2.7× faster in place. Clang never vectorizes the byte loop at SSE2.
- **Against Skia's in-place swap:** 3.5–5.2× faster. Skia's portable loop fails its own
  run-time overlap check in place and runs one pixel at a time. This is the case the review
  asked about.
- **Against Skia on separate buffers:** about level, because Skia's portable loop
  auto-vectorizes there. The kernel is 0.81× at 64 pixels (a loss) and 1.01–1.06× above.

**Reference profile, Skia's run-time dispatch enabled (AVX2 on this CPU).** These are losses,
recorded as such.

| Case | stx d / i | skia (AVX2) d / i |
|---|---|---|
| 64 px | 5.66 / 6.47 | 15.00 / 14.43 |
| row 1920 | 7.81 / 7.23 | 38.92 / 48.32 |
| 300×200 | 8.23 / 7.36 | 14.21 / 23.70 |
| 1920×1080 | 7.65 / 7.22 | 10.55 / 21.65 |
| 3840×2160 | 4.70 / 6.10 | 5.06 / 7.83 |

Skia's 256-bit shuffle is 1.4–6.7× faster in cache and 1.08–1.28× at 4K, where memory
bandwidth dominates. That speed needs AVX2 and run-time dispatch. The candidate's
baseline-SIMD policy and the plan's 128-bit width exclude both, so it is not available to
GUI.Forms as shipped.

**Candidate built with `-march=x86-64-v2`,** which allows SSSE3, as GUI.Forms' wasm build
emulates. Skia's static default becomes its SSSE3 shuffle.

| Case | stx d / i | skia (SSSE3) d / i |
|---|---|---|
| 64 px | 4.93 / 9.16 | 10.41 / 13.28 |
| row 1920 | 7.12 / 13.39 | 14.72 / 14.85 |
| 300×200 | 7.28 / 13.80 | 14.31 / 14.49 |
| 1920×1080 | 6.98 / 13.17 | 9.87 / 14.71 |
| 3840×2160 | 4.89 / 7.44 | 4.59 / 7.66 |

In this binary the kernel's separate-buffer path runs at about half Skia's speed in cache:
0.47–0.51× up to 300×200, and 0.71× at 1080p. It is level at 4K, 1.07×. Its in-place path is
0.90–0.97× from one row up, and 0.69× at 64 pixels.

**Code placement on Zen 5 (negative, unresolved).** Small probe programs linked against the
same v2 object show the gap is placement, not the kernel:
- **Each path, by binary:** the separate-buffer path runs at about 15 Gpx/s in one binary
  and 8 in another; the in-place path behaves the same way.
- **Buffer layout ruled out:** a sweep of all 256 relative buffer offsets in 16-byte steps,
  and of start alignments, was uniformly fast in a binary where the loop was well placed.
- **Loop alignment:** rebuilding with `-falign-loops=32` and `=64` moved which path was
  slow. 64 made every path slow.
- **Interleaving:** a variant with `#pragma clang loop interleave_count(1)` was still
  placement-sensitive.

The rates fit the loop missing the micro-op cache and running from the legacy decoders, about
3 against about 6 instructions per cycle. That was not confirmed with performance counters.
No alignment hint was adopted, because none stabilized it.

The SSE2 build that GUI.Forms ships was stable across every binary and alignment tried
(7.3–8.8 Gpx/s). Its loop is larger and appears decode-bound in every placement.

### Windows (CI, pinned toolchain)

Measured by `.github/workflows/gui-windows.yml`, run 40583645977 at 5cbfe623.
- **Toolchain:** GUI.Forms' pinned MSYS2 CLANG64 toolchain (clang 22.1.8), restored from its
  release.
- **Machine:** an AMD EPYC 7763 (Zen 3) runner with 2 cores and 4 threads.
- **Build:** the candidate profile, at the x86-64 SSE2 baseline GUI.Forms ships.

**Object code.**
- **Standalone kernel:** `swizzle.cpp.obj` has no undefined symbols. Its loops are SSE2 128-bit:
  `pand` ×16, `pslld` ×8, `psrld` ×8, `por` ×16, `movdqu` ×16, `movdqa` ×26, with no AVX.
- **As the fixture builds it:** the object built inside the GUI.Forms fixture, under GUI.Forms'
  `llvm22.cmake`, has identical instruction counts.

**Timing,** in Gpx/s. `skia` equals `skia default` here, because the candidate's baseline-SIMD
policy disables dispatch.

| Case | stx d / i | skia d / i | bytes d / i | scalar d / i |
|---|---|---|---|---|
| 64 px | 3.28 / 4.00 | 3.99 / 1.25 | 0.96 / 1.95 | 0.96 / 1.70 |
| row 1920 | 6.06 / 6.13 | 6.12 / 1.30 | 1.05 / 2.00 | 1.05 / 2.00 |
| 300×200 | 3.02 / 4.56 | 3.29 / 1.15 | 0.92 / 1.73 | 0.93 / 1.69 |
| 1920×1080 | 6.02 / 5.95 | 5.74 / 1.29 | 0.97 / 1.88 | 0.94 / 1.88 |
| 3840×2160 | 3.86 / 4.35 | 3.73 / 1.29 | 0.94 / 1.82 | 0.92 / 1.82 |

- **Against GUI.Forms' byte loops:** the kernel is 3.3–6.2× faster between separate buffers and
  2.0–3.2× faster in place.
- **Against Skia's in-place swap:** 3.2–4.7× faster.
- **Against Skia between separate buffers:** about level, 0.82–1.05×. The losses are at 64
  pixels and 300×200.

This agrees with the Linux x86-64 pod.

## Not done
- **wasm32:** SIMD128 object-code inspection.
- **GUI.Forms integration:** since done in the pinned fixture; see
  `SWAP_KERNEL_GUI_INTEGRATION.md`. Its two Windows files are not yet built.

## Gates

On the M4:
- **Candidate:** 88/88, including `swizzle_contract` and `swizzle_without_skia`.
- **Reference:** 80/80.
- **Instrumented suite:** PASS.
- **Shared artifacts:** unchanged, with the five intentional differences as before.
- **Installed consumer:** passes from a fresh relocated install.

The contract test also passed on the Linux pod's candidate, reference and v2 builds.
