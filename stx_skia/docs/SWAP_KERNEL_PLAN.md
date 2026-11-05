# Standalone red/blue swap kernel: plan — 2026-10-27

Status: implemented, and integrated into the pinned GUI.Forms fixture. The Windows and wasm
steps, and a Windows build of the integration, remain. See
`results/2026-10-27/SWAP_KERNEL.md` and `SWAP_KERNEL_GUI_INTEGRATION.md`.

GUI.Forms renders BGRA8 premultiplied sRGB everywhere. Several hosts must hand pixels to
something that wants RGBA byte order, or take RGBA pixels in:
- **Android** (planned): it copies damaged rectangles of the back buffer into an
  `ANativeWindow` buffer, which is RGBA_8888 or RGBX_8888.
- **X11:** the named fallback kernel for a visual whose masks are not BGRA.
- **Existing loops:** the scalar byte loops listed under GUI.Forms integration below.

The owner asked for Skia's optimized swap to become a standalone method that GUI.Forms can
call without depending on Skia internals.

## What exists

`SkOpts::RGBA_to_BGRA` is in `skia/src/opts/SkSwizzler_opts.inc`. Its public wrapper is
`SkSwapRB` (`include/core/SkSwizzle.h`). Its versions:

| Target | Skia version | Width |
|---|---|---|
| arm64 NEON | `vld4q_u8`/`vst4q_u8` with the R and B planes swapped, 16 then 8 pixels | 128-bit |
| x86 SSSE3 | `_mm_shuffle_epi8`, 4 pixels | 128-bit |
| x86 AVX2 | `_mm256_shuffle_epi8`, 8 pixels | 256-bit |
| LoongArch LSX/LASX | byte shuffles | 128/256-bit |
| Everything else, including baseline x86-64 (SSE2) | scalar loop, one pixel at a time | — |

Two problems make it unsuitable to call directly:
- **It goes through Skia's dispatch.** The function pointers are filled in by `SkOpts`
  dispatch, which checks the CPU at run time. Calling it means calling into Skia's
  global initialization.
- **Its baseline x86 version is scalar.** Without the run-time SSSE3 upgrade, x86 has no
  SSE2 version. Builds at the x86-64 baseline that skip dispatch get the one-pixel loop. This
  is the case the new kernel exists to beat. Under GUI.Forms' wasm build (`-msse4.1`),
  `SkFeatures.h` sets the x86 level from `__SSE4_1__`, so the static default compiles the
  SSSE3 shuffle, which Emscripten's SSE headers map onto wasm SIMD128. This was read from the
  source; nothing was built for wasm.

Skia's version stays as imported baseline code. It is the reference for correctness and speed.

## Location

The kernel lives in stx_skia's own interface, beside `take_pixel_data`:
- `include/stx_skia/swizzle.hpp`, the header;
- `src/swizzle.cpp`, compiled with the library's target flags.

The owner's first suggestion was GUI.Forms' core. stx_skia is acceptable on two conditions,
and both are checked, not assumed:
- **No Skia headers.** `swizzle.hpp` and `swizzle.cpp` include only standard headers. A test
  compiles the header with Skia's include directories absent.
- **No Skia code pulled in.** A host can link the swizzle object without pulling in Skia's
  own code. `nm -u` on `swizzle.o` must list only C and C++ runtime symbols (`memcpy`
  and the like). A test program that calls only the kernel, linked against the stx_skia
  archive, must contain no Skia symbols. Skia is required on every platform, so linking the
  archive is never an extra dependency.

## Design

**Byte order.** On a little-endian target, a pixel loaded as `uint32_t` holds A, then R or
B, then G, then the other colour, from high byte to low. The formula below depends on that.
Every GUI.Forms target is little-endian, wasm included. Both `swizzle.hpp` and
`swizzle.cpp` state it with
`static_assert(std::endian::native == std::endian::little, "...")`, so a big-endian build
fails to compile instead of producing swapped green and alpha.

**Kernel form.** Swapping red and blue is:

```
out = (in & 0xFF00FF00) | rotate_left(in & 0x00FF00FF, 16)
```

That is two ANDs, a rotate and an OR on 32-bit lanes. It works at the 128-bit common width
on all three targets:
- **SSE2:** `pand`, `pslld`/`psrld`, `por`;
- **NEON:** the rotate may become `rev32` on 16-bit lanes;
- **wasm SIMD128:** the same lane operations.

No intrinsics, no target attributes and no run-time dispatch are needed. Pixels are loaded
and stored with `std::memcpy` to and from `uint32_t`, so byte buffers at any alignment are
allowed. The scalar loop should be compiler-visible enough to vectorize.

**An alternative to measure.** A byte-permutation loop
(`d[4i] = s[4i+2]`, `d[4i+2] = s[4i]`, and so on) may lower to NEON `ld4`/`st4` or
`tbl`, like Skia's version. The mask form is the default. The byte form is kept only if
it measures faster on a target and still vectorizes on the others. Hand-written
intrinsics are a last resort and would need a measured case.

**Units.** Width, height and strides are different quantities. Android reports its window
stride in pixels; GUI.Forms' strides are in bytes. The house style says to distinguish
types whose confusion causes bugs, so each gets its own type:

```cpp
namespace stx_skia {
struct PixelCount { std::size_t value = 0; };  // whole 4-byte pixels in a row
struct RowCount   { std::size_t value = 0; };  // rows
struct RowBytes   { std::size_t value = 0; };  // distance between row starts, in bytes

// Converts a stride given in pixels, as Android reports it, to bytes. It is empty if the
// byte count overflows.
[[nodiscard]] std::optional<RowBytes> row_bytes_for(PixelCount stride);
}
```

A `PixelCount` cannot be passed where `RowBytes` is expected, so an Android stride must go
through `row_bytes_for`.

**Interface.**

```cpp
namespace stx_skia {
enum class SwizzleResult {
    ok,
    size_not_whole_pixels,  // a span's size is not a multiple of 4
    size_mismatch,          // swap_red_blue: dst and src differ in size
    row_too_narrow,         // a stride is less than width x 4 bytes
    extent_overflow,        // a required extent does not fit in std::size_t
    buffer_too_short,       // a span is shorter than its required extent
    partial_overlap,        // the buffers overlap, but not as an exact in-place call
};

// Swaps red and blue in each 4-byte pixel. dst and src have the same size, a whole
// number of pixels, and are either the same span (in place) or disjoint. Nothing is
// written unless the result is ok.
[[nodiscard]] SwizzleResult swap_red_blue(std::span<std::byte> dst,
                                          std::span<const std::byte> src);

// The same over width x height pixels of rows with independent strides, as for one damage
// rectangle. All validation finishes before any pixel is written. Bytes past each row's
// width, and bytes between rows, are never read or written.
[[nodiscard]] SwizzleResult swap_red_blue_rows(std::span<std::byte> dst, RowBytes dstStride,
                                               std::span<const std::byte> src,
                                               RowBytes srcStride, PixelCount width,
                                               RowCount height);
}
```

**Validation in `swap_red_blue_rows`,** for each buffer separately:
1. **Row width.** `rowBytes = width x 4` is computed with an overflow check
   (`extent_overflow`).
2. **Stride.** The stride must be at least `rowBytes` (`row_too_narrow`). This applies
   whenever `height > 1`. With one row the stride is never used, and any value is
   accepted.
3. **Extent.** The required extent is `stride x (height - 1) + rowBytes`. The product and
   the sum each get an overflow check (`extent_overflow`), and the span must be at least
   that long (`buffer_too_short`).
4. **Empty case.** `width == 0` or `height == 0` succeeds and writes nothing, once the
   checks above pass.

**Aliasing.**
- **`swap_red_blue_rows`:** in place means exactly the same start address and the same
  stride, with the same width and height. Any other intersection of the two extents is
  `partial_overlap`, including the same start with different strides.
- **`swap_red_blue`:** in place means the same data pointer and size; any other intersection
  is `partial_overlap`.
- **No interleaving.** The check is conservative: two images whose rows interleave in one
  allocation without touching are still rejected. No caller needs that case.
- **No `restrict`.** The in-place case rules out a `restrict` contract. If disassembly
  shows that the possible aliasing blocks vectorization, the fix is a separate disjoint
  loop chosen after the overlap check, not an annotation.

**Alpha.** Alpha passes through unchanged. RGBX_8888 ignores that byte, so the same kernel
serves both Android window formats.

**Rectangle sets.** GUI.Forms validates its own damage-rectangle sets before writing, then
calls `swap_red_blue_rows` once per rectangle (owner decision, 2026-10-27). stx_skia offers
no rectangle-set entry point.

## Evidence

1. **Correctness against Skia.** A differential test compares `swap_red_blue` with
   `SkOpts::RGBA_to_BGRA`, and with a byte-by-byte reference:
   - pixel counts 0 through 64 and long rows (every tail length for 4-, 8- and 16-pixel
     blocks);
   - byte offsets 0 through 3 on both buffers (misaligned spans);
   - in place and disjoint.

   Outputs must be byte-identical.
2. **Round trip.** Swapping twice gives back the original, byte for byte:
   - for every tail length and misaligned start above;
   - in place and between separate buffers;
   - for `swap_red_blue_rows` over strided rectangles.
3. **Rows and validation.** Strided rectangles must keep padding and inter-row sentinels
   intact.
   - **Failures:** each failure result has a test that checks the destination is untouched:
     - a stride narrower than `width x 4`, for either buffer;
     - `width x 4` overflowing;
     - `stride x (height - 1)` overflowing;
     - the sum with the row width overflowing;
     - a span shorter than its extent;
     - sizes that are not whole pixels;
     - mismatched sizes;
     - partial overlap: the same start with different strides, an offset start, and a
       destination that begins inside the source's last row.
   - **Successes:** zero width and zero height succeed without writing.
   - **Stride conversion:** `row_bytes_for` is tested at its overflow boundary.
   - **Types:** a compile-time check confirms that `PixelCount` does not convert to
     `RowBytes`.
4. **Generated code.** Inspect the object code of each build for a 128-bit vector loop and
   record the instructions:
   - native arm64 with the stx_skia toolchain;
   - **Windows:** the compiler GUI.Forms ships. That is MSYS2 CLANG64 clang 22.1.x, targeting
     `x86_64-w64-windows-gnu`, restored by `tools/windows_toolchain.py` and used through
     `D:/gui_forms_w5/gui_forms/cmake/llvm22.cmake`, with GUI.Forms' Windows flags.
     Windows is the largest user base, so this inspection decides whether the portable form
     is enough.
   - x86_64 at the SSE2 baseline and at `-msse4.1`;
   - `wasm32` with `-msimd128`, using the LLVM wasm backend; Emscripten is not needed to
     inspect code generation.
5. **Measurements,** following `results/2026-10-26/MIPMAP_KERNELS.md`:
   - **arm64 (M4 mini):** the mask kernel, the byte-permutation kernel, Skia's NEON
     version and the scalar byte loop.
   - **x86-64 (the Windows machine):** the same kernels, built with the pinned Windows
     toolchain. On x86 the Skia comparison includes the scalar SSE2-baseline version and
     the SSSE3 and AVX2 versions.
   - **Inputs:** 64 pixels, one 1920-pixel row, a 300x200 damage rectangle with a 4096-byte
     stride, and full 1920x1080 and 3840x2160 frames, in place and disjoint.
   - **Machine load:** the Windows VM's earlier records note heavy load from other builds,
     so each timing records the load.
   - **Reporting:** record losses as well as gains. Claim wasm speed only from code
     inspection.
6. **Gates.** Candidate, reference and the instrumented suite. The installed-consumer test
   includes `stx_skia/swizzle.hpp` and calls both functions.

## GUI.Forms integration (later, as one reviewable patch in integration/)

The authoritative GUI.Forms checkout is not modified. The patch is made against the pinned
fixture, rebased to GUI.Forms `main`; it was read at 62d8499d. It replaces every existing copy
of the loop:
- **macOS:** the BGRA-resource-to-RGBA byte loop in `coregraphics_raster.cpp`. This becomes
  `swap_red_blue`.
- **`raster_canvas.cpp`:** `rgba_to_bgra`, which also allocates a new vector for each tile.
  It becomes `swap_red_blue_rows` into storage that is reused across tiles.
- **`drawing_platform_windows.cpp`:** `copy_as_bgra`, which copies and then swaps in place in
  a second pass. It becomes one disjoint `swap_red_blue` for RGBA snapshots and a plain copy
  for BGRA ones.
- **`windows_host.cpp`:** the live-surface conversion into `live_bgra_scratch_` in
  `draw_live_surface_frame`. It becomes `swap_red_blue`.
- **Later:** the X11 non-BGRA fallback and the Android present path, using
  `swap_red_blue_rows` per damage rectangle after GUI.Forms has validated the set.

Changing the native live-surface format to BGRA on every platform is a separate GUI.Forms
decision that goes with that patch. It is not part of the kernel.

## Not in scope

- Premultiply or unpremultiply variants, gray or RGB expansion, and other `SkSwizzler`
  routines. They can follow the same pattern if a consumer needs them.
- Converting or removing Skia's own `SkSwizzler_opts`. That is imported optimized code,
  and it stays the baseline.
- The Android NDK and wasm build profiles. Those are separate work, recorded in project
  memory as required targets.
