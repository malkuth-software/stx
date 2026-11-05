# Font machinery and GUI.Forms' A8 mask path — 2026-10-27

## What GUI.Forms uses

GUI.Forms' Skia references at its fixture pin (8d1f2be0) and on `main` (62d8499d) include no
font, typeface, text-blob, glyph, strike or scaler-context API. Its Skia surface is:
- the canvas and paint;
- rectangles, rounded rectangles, paths, regions and matrices;
- images, surfaces and pixmaps;
- shaders, gradients and colour filters;
- blur mask filters and shader mask filters;
- dash path effects;
- one stream, and the PNG encoder.

Text reaches Skia only as pixels. GUI.Forms shapes and rasterizes glyphs with its own
HarfBuzz/FreeType engine, then draws the coverage as `kAlpha_8` images with a paint
(`prepared_skia_frame.cpp`), plus mask filters for blur. So the A8 path needs:
- A8 image drawing (sampling and shading);
- mask filters and blur;
- mask blitting.

It needs none of the font machinery. Those are covered by the existing corpora: canvas A8 image
draws, blitters, image subsets, masks and mask filters.

## What the candidate still compiled

`compile_commands.json` for the candidate lists no scaler-context, strike, glyph, typeface or
custom-typeface source. Earlier pruning had already excluded them. The last unit's ledger said
they were still compiled; that was wrong. The symbol check behind it read stale object files
left in the build directory by sources no longer compiled. The closure script now considers
only objects of compiled sources.

Of the named facilities, only `SkMatrix22.cpp` was compiled, and nothing used it. The same
check covered `src/utils/win`: `SkAutoCoInitialize`, `SkDWriteGeometrySink`, `SkHRESULT`,
`SkIStream`, and the COM pointer and object-base headers. On Windows these compile into the
library, but their only users were the WIC codecs, XPS and the DirectWrite font backends, none
of which the candidate builds. Nothing else in a Windows-compiled source refers to them.

## Change

- **Moved unchanged:** `SkMatrix22.cpp/.h` and all of `src/utils/win`, 14 files, to
  `baseline/skia`. They were unchanged since intake.
- **Reference build:** it compiles the baseline sources. The Windows ones are guarded by
  `SK_BUILD_FOR_WIN` and compile to nothing elsewhere.
- **Absence check:** `tests/canvas_device.cpp` checks that their headers are gone.

## Verification

- **macOS gates:**
  - candidate: 88/88;
  - reference: 80/80;
  - instrumented suite: PASS.
- **Shared artifacts:** unchanged.
- **Windows:** its candidate and reference builds are checked by the Windows CI job on PR #1.
- **Inventory:** the AST inventory is at 801 constructs in 58 files.

## Remaining dead source

The candidate compiles sources from seven `src` directories only: `core`, `shaders`, `image`,
`effects`, `utils`, `ports` and `lazy`. These upstream directories are still in `skia/src` but
contribute nothing to the build:

| Directory | Files |
|---|---|
| `gpu` | 647 |
| `codec` | 94 |
| `pathops` | 58 |
| `pdf` | 47 |
| `encode` | 22 |
| `partition_alloc` | 8 |
| `xml` | 7 |
| `xps` | 4 |
| `svg` | 4 |
| `android` | 3 |
| `capture` | 1 |

Moving them out of the candidate is a separate tree-cleanup unit.
