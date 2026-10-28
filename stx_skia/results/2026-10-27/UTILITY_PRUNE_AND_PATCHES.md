# Unused utility removal and patch utilities — 2026-10-27

## Link closure

A symbol-level check of the candidate archive (`llvm-nm` defined symbols of each object
against undefined symbols of every other archive member and test program, restricted to
sources in `compile_commands.json`) gave:

| Facility | Users | Outcome |
|---|---|---|
| `SkDocument` (+ `include/docs`, `SkMultiPictureDocumentPriv.h`) | none compiled (`SkMultiPictureDocument.cpp` is not built) | Removed |
| `SkCamera` | none | Removed |
| `SkJSONWriter` | none | Removed |
| `SkDistanceFieldGen` | none | Removed |
| `SkParse` | `SkParseColor`, `SkParsePath` only | Removed with them |
| `SkParseColor`, `SkParsePath` | none | Removed |
| `SkFloatToDecimal`, `SkOSPath` | none (found by the same check) | Removed |
| CoreGraphics bridge (`SkCGUtils.h`, `SkCreateCGImageRef.cpp`, `SkCGBase.h`, `SkCGGeometry.h`) | none | Removed |
| `SkUniqueCFRef.h` | `tests/template_helpers.cpp` | Kept |
| `SkPatchUtils` | `SkDevice::drawPatch` | Kept; converted and fixed |
| `SkEventTracer` | no symbol users, but tracing headers include it | Left for the tracing support unit |

GUI.Forms calls none of the removed facilities. The 27 removed files were unchanged since
intake, so they move as they are to `baseline/skia`, and the reference build compiles the
baseline sources (the bridge only on Apple). Absence checks are in `tests/canvas_device.cpp`
and the installed-consumer test.

## Patch utilities

**Evidence before any change.**
- `tests/patch_utils.cpp` writes `patch-utils.raw` (298 cases, 30.7 MB):
  - levels of detail for random Coons patches over four coordinate spans and seven matrices,
    including perspective, plus degenerate and non-finite patches;
  - the full vertices built from those levels and from explicit ones, covering the
    10000-vertex and 200-step reduction and the refusals below one;
  - every case with and without colours and texture coordinates, across three colour spaces.
- The intake `SkPatchUtils.cpp/.h` were copied to `baseline/skia`, so the reference keeps the
  original.
- Both builds wrote identical bytes, and the canvas corpus (`canvas-device.raw`) also draws
  patches.

**Mechanical rewrite (dc801b23).**
- **Spelling:** `->` becomes `(*p).`, `auto` vector types are spelled out, one name per
  declaration, single-assignment locals are `const`, and locals lose the member prefix.
- **Evaluator:** its members are initialized, and its unused `done()`, with the step count
  only `done()` read, is dropped.
- **Evidence:** every shared artifact is identical.

**Fixes (this commit):**
- **Out-of-range conversion.** `GetLevelOfDetail` converted `length / 10` to `int` with
  `static_cast`. A finite patch a few times 10^10 units across made that undefined
  behaviour. It now saturates.
- **Integer overflow.** `MakeVertices` formed `lodX + 1` and `lodX + lodY` in `int`, which
  overflows for levels near `INT_MAX`. Its counts are now taken in 64 bits.
- **Large patches vanished.** `MakeVertices` refused any level whose vertex count passed
  `INT_MAX`, before its own reduction to at most 200 steps. A patch about a million device
  pixels across on both axes therefore drew nothing, while a slightly smaller one drew
  reduced. The reduction now comes first, so every level up to `INT_MAX` tessellates.
  - **Behaviour change:** recorded in `patch-utils-large.raw`, which is meant to differ (22
    cases). For patches 3×10^6 to 8×10^7 units across, both builds compute the same levels,
    for example 1,247,044 by 1,053,716. The reference returns no vertices for all 22 cases,
    while the candidate tessellates them.
- **Matrix pointer.** `GetLevelOfDetail` takes its matrix by reference instead of an
  unchecked pointer.
- **Documentation.** The header documents both functions' contracts.
- **Candidate-only checks.**
  - Patches with coordinates near 10^20 saturate their level of detail and tessellate into
    at most 201×201 vertices.
  - `INT_MAX` levels tessellate in every pairing.
  - A non-finite edge still gives {0, 0}, which `MakeVertices` refuses.

## Verification

- **Builds:**
  - candidate: 88/88;
  - reference: 80/80;
  - instrumented suite: PASS. UBSan would report the old float-to-int conversion on the
    huge-patch checks.
- **Shared artifacts:** `patch-utils.raw` has the same hash in both builds, and every other
  shared artifact is identical. The intentional differences are the earlier five plus
  `patch-utils-large.raw`.
- **Inventory:** the AST inventory falls from 1,055 excluded constructs in 70 files to 802
  in 59.
