# Compiled code nothing reaches — 2026-10-27

The first step of the remaining work. Of the 526 files the candidate compiles or includes, 171
(27,592 lines) had never changed since intake. Before converting them, this check removed the
compiled code that nothing reaches.

## Method

`tools/link_closure.py` ran over all 212 compiled library sources. A source is dead when no live
library object and no test references any external symbol it defines. "Live" was computed to a
fixed point, so code referenced only by dead code is dead too. Twenty-one objects came out dead.

**Public API leaves are excluded.** Gradients, colour filters, mask filters and the like have no
internal user, but GUI.Forms and other consumers call them; the tests exercise them. They stay.

**Removed in this step: internal code, with no API a consumer could call.**

| Code | Why it is dead | Lines |
|---|---|---|
| The legacy bitmap shader-context path: `SkBitmapProcShader`, `SkBitmapProcState`, its matrix procedures, and the `SkBitmapProcState_opts` kernels (portable, SSSE3, LASX) | `SkImageShader` reaches it only under `SK_ENABLE_LEGACY_SHADERCONTEXT`, which nothing defines. Images are drawn through the raster pipeline. `SkGraphics::Init` still installed the kernels' function pointers, which nothing read | ≈2,300 |
| Tracing: `SkTraceEvent.h`, `SkTraceEventCommon.h`, `SkATrace`, `SkEventTracer` (with `include/utils/SkEventTracer.h` and `SkTraceEventPhase.h`) | The library target defines `SK_DISABLE_TRACING` publicly, so every trace macro is empty. The only uses were 18 `TRACE_EVENT0` lines in `SkCanvas.cpp` | ≈1,480 |
| `SkSLTypeShared` | Shading-language type tables; there is no SkSL in a CPU library | 484 |
| `SkSpinlock` | Nothing locks one: no object references its slow path | 112 |
| `SkMasks` | Bit-mask helpers for the codecs, which live outside this library | 210 |
| `SkSqrtBits`/`SkSqrt32` and `SkHexadecimalDigits` | Used only by the emboss mask, which the candidate does not build, and by `SkString::insertHex`, pruned in the string unit | ≈60 |
| `SkDiscardableMemory_none.cpp` | A port whose `Create` nothing calls | 16 |

## Also changed

`include/android/SkImageAndroid.h` keeps only `SkImages::RasterFromBitmapNoCopy`, the one
function `SkImage_AndroidFactories.cpp` defines. The removed declarations were:
- the hardware-buffer and texture-pinning factories, which need the GPU backend;
- `PinnableRasterFromBitmap`, which had no definition anywhere.

That header was the candidate's only route to `include/gpu`. Now no compiled source includes a
GPU header (`ninja -t deps` lists none).

**Kept, though no test calls it.** `RasterFromBitmapNoCopy` is a CPU entry point for Android,
which the owner may use.

## Provenance and the reference build

The removed files moved unchanged to `baseline/skia`. Where the candidate had changed a file
since intake (`SkBitmapProcState_opts.cpp`), the baseline holds the intake version. The baseline
also holds intake copies of the headers the candidate trimmed (`SkUtils.h`, `SkMathPriv.h`,
`SkImageAndroid.h`). The reference build compiles all 13 moved sources from there.

## Verification

- **Candidate:** 105/105.
- **Reference:** 86/86.
- **Corpora:** every shared artifact is identical apart from the intentional differences. No
  warnings.
- **Instrumented suite:** PASS.
- **Installed package:** passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9.
- **Inventory:** the AST inventory falls from 309 excluded constructs in 44 files to 276 in 35.

## Not removed: public API with no caller

These have no caller in the library, the tests or GUI.Forms. They are public API, and the owner
decided to keep them; they are converted with their units:
- `SkDataTable` and `SkPoint3`;
- `SkTiledImageUtils` and the Perlin-noise shaders (`MakeFractalNoise`, `MakeTurbulence`);
- `SkMaskFilter::Deserialize` (effect deserialization is disabled);
- `SkTime`, `SkFloatInterpFunc`, and the out-of-line `SkCanvasPriv` lattice helpers.

## Not done

- **Windows and Linux:** CI covers the x86 build on push.
- **Leftover preprocessor block:** `SK_ENABLE_LEGACY_SHADERCONTEXT` still guards the generic
  shader `Context` machinery in `SkShaderBase`, which other shaders declare. Defining it is not
  supported.
