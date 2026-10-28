# stx_skia: what GUI.Forms needs from a CPU rasteriser

Written 2026-10-25 by the coordinating session for the owner, from the
current source on main. `src/render/skia/` is the only Skia consumer; the
counts below are how often each Skia type or call appears there and in the
host adapters.

## How GUI.Forms uses Skia today

- **Painters.** Every painter implements `Painter`
  (`include/gui_forms/types/painter/painter.hpp`):
  - `SkiaRaster`: macOS and X11.
  - `CoreGraphicsRaster`: macOS paths.
  - `DibPainter`: Windows, first-party, no Skia.
  - `RecordingPainter`: the display list.

  The Windows host links no Skia at all.
- **Text.** Text never goes through Skia. Our engine (HarfBuzz, FreeType,
  our own shaping, line breaking and A8 glyph masks) produces coverage
  masks, and the painter only composites them. Skia's font manager and
  CoreText host are compiled in, but carry no text policy of ours.
- **Effects.** The T2 engine does its effects first-party on its own CPU
  raster: materials, glass, glow, procedural materials. Skia draws the
  primitives the engine and controls ask for.
- **Output.** One host contract: premultiplied RGBA8, sRGB, CPU memory the
  host copies to the screen.

## Required: the current use, which must match bit-for-bit or within stated bounds

**Surfaces and pixels.**
- Raster surfaces over caller-owned memory: wrap pixels, row bytes, RGBA8
  and BGRA8 premultiplied, A8 masks.
- Read and write pixels, clear.
- An image from caller pixels, with or without a copy.

**State.**
- save, restore, restoreToCount and the save count.
- translate, scale, set and reset the matrix, read the total matrix. Only
  affine transforms are needed today; rotation and skew are used only by
  "don't snap" tests.

**Clips.**
- Rectangles, rounded rectangles (single radius), paths.
- Regions (rectangle sets, union and intersect), used for damage clipping.
- Device clip bounds, and an "is the clip a rectangle" query.
- Clips always intersect; difference clips are unused.

**Fills and strokes.**
- Rectangle, rounded rectangle, oval, line and path.
- Fill and stroke styles; winding and even-odd fill rules.
- Dashed strokes (one use).
- Antialiasing on or off per draw. Pixel snapping in PR #340 relies on
  turning it off at snapped edges.

**Paths.** A path builder with moves, lines and curves: only what rounded
shapes and the few vector parts use.

**Gradients.**
- Linear and radial gradients; clamp, repeat and mirror tile modes.
- Premultiplied interpolation.
- Colour stops as float colours.

**Images.**
- Draw an image or a sub-rectangle of one, with nearest or linear sampling
  and a strict source-rectangle constraint, so tiles don't bleed.
- Image patterns tiled with repeat.
- Opacity per draw.

**Effects.**
- Gaussian blur mask filter, for box shadows (outer and inset).
- A shader-as-mask filter (one use).
- A 4×5 colour-matrix filter (one use).

**Blending.** source-over and source only.

**PNG.**
- Decode and encode, for the clipboard and resources.
- PNG is the only codec admitted: AGENTS.md hard boundary. stx_png already
  exists in this repository and could take this over entirely.

## Constraints, from AGENTS.md (hard)

- CPU only. No GPU backend of any kind may be compiled, linked or probed:
  Ganesh, Graphite, GL, Vulkan, Metal, D3D, Dawn, WebGPU.
- No Skia type in any public header, the DML, the C ABI or core tests.
  Everything stays behind `src/render/<renderer>/`.
- No codec but PNG(caveat from human also see stx_CNG, which we want to wire in, and which is being rolled out
to frozen version 9.0 within the hour with our decided final design)
- Platforms: Windows x64 (MSYS2 CLANG64), macOS arm64, Linux x64 and
  arm64. Today's MinGW Skia archive is BGRA, and the others are RGBA.
- Deterministic output for a given input on every host. Our engine is
  the reference: goldens are ours, never a system renderer's.
- House style, if stx_skia is first-party code in this repository like
  stx_zlib and stx_png: sanitizer-clean, effort bounds as counts.

## Wanted: what Skia allows that we don't use yet

In rough order of value to the programme now in flight:

1. **Group opacity and layers** (`saveLayer` with alpha and bounds). For
   fades and animated show and hide (T6 animation, ribbon), and correct
   opacity of overlapping children. Not used today.
2. **General affine transforms with antialiasing**: rotation and scale, for
   animation, rotated text masks and spinners. Only translate and scale are
   used today.
3. **Sweep (conic) gradients**: ColorDialog's colour wheel, progress rings
   and focus sweeps. Not in `Painter` today.
4. **Blend modes beyond source-over**: multiply, screen, and darken and
   lighten, for T3's blend support and themed tinting of icons.
5. **Arbitrary antialiased vector paths in `Painter`**: icons, check marks
   and chevrons drawn from vectors instead of bitmaps, at any scale.
   Paths are internal to the painter today.
6. **High-quality downscaling of images** (mipmaps or cubic sampling): icon
   and thumbnail quality at fractional scales.
7. **Per-corner radii** (`SkRRect` with four radii): tabs, attached popups
   and segmented controls.
8. **A backdrop blur of what's already drawn** (an image filter on a layer),
   as an alternative or check for the engine's glass. Optional: the
   engine's own glass is the reference.

## Stance on the build

Today's Skia is the `chrome/m152` tree, fetched locally (never committed).
It's built with an audited GN profile (`third_party/skia_cpu_args.gn`) and
a PNG-only patch, then published by CI as a cached package per platform.

stx_skia can take any route:
- a pruned and patched Skia, vendored and built by our own CMake;
- a first-party rasteriser covering exactly the lists above.

The requirements are the same either way. To use it universally, the
Windows host would add it as a second `Painter` beside `DibPainter`, the
way macOS keeps `CoreGraphicsRaster`, chosen when the program starts and
reported as a capability. The DIB painter stays the native reference
behaviour.


## Owner clarification after intake — 2026-10-25

The owner withdraws optional item 8 above. GUI.Forms performs all CSS image and
backdrop filtering on its own raster. Skia's general SkImageFilters system is
outside the retained contract and is to be removed. Keep these separate effects:
SkMaskFilter::MakeBlur for painter shadows, SkColorFilters::Matrix and
SkShaderMaskFilter. Layers for opacity remain required.

All Skia text/font machinery is to be removed: GUI.Forms supplies finished glyph
coverage masks. Composition of those masks remains required. Image codecs remain
outside stx_skia, including PNG, already owned by stx_png. Remove SkSL/runtime
image effects after resolving internal CPU dependencies. GPU, PDF, SVG, Skottie,
tracing and tools stay excluded. This clarification supersedes the earlier
optional backdrop requirement; it does not claim the pruning is already done.
