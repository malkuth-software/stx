# GUI.Forms Skia intake — 2026-10-25

## Baseline

GUI.Forms main: `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`.
Skia: `2a9b593bab4b2fd019fa494c8d401ff1fab0b883`.
All consumer paths below are relative to that GUI.Forms revision. Source
references are observations, not runtime profiles or proof of binary reachability.

There are two principal consumers: `src/render/skia/raster/skia_raster.cpp`
(control rendering) and `src/render/skia/executor/skia_executor.cpp`
(GUI.Drawing). `raster/prepared_skia_frame.cpp` adds optional prepared text-mask
composition. Looking only at control rendering would omit real GUI.Drawing work.

## Retain

| Facility | Observed consumer | Required behavior |
|---|---|---|
| CPU surfaces and pixel views | Raster frame setup; executor `WrapPixels`; prepared frame | Owned and borrowed buffers, row strides, RGBA/BGRA premultiplied pixels, A8 masks, snapshots and pixel access |
| Canvas state and transforms | Both consumers | Save/restore, translations, scaling and GUI.Drawing affine matrices |
| Clipping | Damage regions, rectangle and rounded clipping; inset shadows | Integer region clipping and antialiased rounded clips; intersection; inset shadows use an even-odd complement path |
| Shape rasterization | Both consumers | Rectangles, rounded rectangles, lines, ellipses, polygon and Bézier paths, fill rules, antialiasing and strokes |
| Dash path effect | GUI.Drawing `configure_pen` | Preset and custom dash sequences |
| Gradients | Both consumers | Linear/radial gradients, stops, opacity, clamp/repeat/mirror; preserve interpolation and rounding |
| Image shaders and sampling | Image draws and texture/hatch brushes | Source rectangles, nearest/bilinear sampling, repeated/mirrored tiles and strict source bounds |
| Compositing | Both consumers | Source and source-over exposed directly; retain internal coverage and mask blend dependencies |
| Colour-matrix filter | GUI.Drawing `apply_image_attributes` | Existing image colour adjustments |
| Blur mask and shader mask filters | Outer/inset shadows and retained shadow masks | A8 coverage generation, blur, mask reuse and placement |
| Alpha-mask image composition | Shared text engine and prepared frames | Draw externally rasterized text coverage without Skia font shaping/rasterization |
| Colour arithmetic | sRGB surfaces, gradients and pixel conversions | Preserve current output; skcms cannot be deleted merely because ICC image input is excluded |

The core CPU raster pipeline, scan conversion, geometric calculations, pixel
blitters and their internal dependencies remain the substance of this library.

## Already excluded by GUI.Forms build policy

Ganesh, Graphite, GL, Vulkan, Metal, D3D, Dawn/WebGPU; PDF, SVG, XPS; Skottie and
module builds; image decoders/encoders and zlib; platform font backends,
Skia's FreeType/HarfBuzz/ICU integrations; tracing and optional tools/Rust targets.
These exclusions already exist. Repeating them is not a newly measured reduction.

Image bytes enter through `src/core/image/`, including CNG resources and PNG
interchange. Skia sees decoded pixels. Text is shaped/rasterized through the
shared GUI.Forms HarfBuzz/FreeType stack. Keep A8 image composition even if Skia's
own glyph machinery is removed.

The owner identifies CNG v9 as the current intended codec. GUI.Forms' checked-in
`stx_cng/PROVENANCE.md` still describes its older rc1/CSP-v2 intake. This intake
does not update codecs or infer v9 behavior from that older copy. The raster
library stays codec-neutral in either case.

## Candidates for the first pruning experiments

| Candidate | Evidence | Dependency question before removal |
|---|---|---|
| Generic codec/encoder scaffolding and Android codec adapters | `skia/BUILD.gn` still adds shared codec, encoder and Android source groups with concrete codecs disabled | Which image/stream utilities remain referenced by CPU images? |
| Picture recording, playback and serialization | No direct SkPicture consumer call found; `SkCanvas.cpp` contains picture entry points and a `SK_DISABLE_SKPICTURE` branch | Can the unused public surface be cut while preserving canvas and flattenable dependencies? |
| Skia text blobs, strikes and font internals | No Skia text drawing in the consumer; `SkCanvas.cpp` still contains text-blob paths | Untangle canvas/device dispatch without removing A8 masks used by our own text engine |
| General image-filter graph | No direct `SkImageFilters` or `setImageFilter` calls found; BUILD.gn adds image-filter sources | Preserve blur MASK filters; they are used and are a different facility |
| SkSL/runtime-effect compilation | No direct runtime-effect creation found; core SkSL groups remain in BUILD.gn | Image filters use known runtime effects internally; remove their dependencies before claiming SkSL is unnecessary |
| Unused effects, formats, sampling and blend specializations | Direct use is narrower than upstream API | Prove internal closure and future material requirements; do not delete by enum grep |

The initial commit preserves the full upstream snapshot. The first CMake candidate
removes excluded implementation/infrastructure sources listed in
`baseline/pruned-source-files.json`; relevant upstream test sources remain.
Source pruning follows the measured compile closure and the coordinator contract.

## Optimization leads from consumer inspection

1. `SkiaRaster::synchronize_images` decodes into a vector and then uses
   `SkData::MakeWithCopy`. A decoder-to-owned-pixel allocation or explicit owner
   transfer could avoid a full image copy. Establish ownership and lifetime first.
2. GUI.Drawing `draw_image` obtains `remapped_pixels` and copies again into
   SkData. Texture brushes have a similar path. Measure unchanged-image reuse
   and conversion costs separately. These are consumer-side opportunities,
   not evidence that a Skia kernel is slow.
3. Gradient setup creates colour/position vectors. Reuse prepared state where
   consumer lifetime permits; do not introduce a universal cache architecture.
4. Retained shadow masks and glyph coverage already have reuse paths. Measure
   cache hits and misses before proposing another cache.
5. Inside Skia, inspect the exercised scan-conversion, sampling, compositing and
   blur kernels. Distinguish removing work from changing arithmetic or rendering.

## Measurement limits

GUI.Forms' retained 2026-10-21 CI record reports macOS arm64 libskia.a at
5,239,168 bytes and libskcms.a at 73,840 bytes. Linux x64 is 10,241,702 and
192,846; Linux arm64 is 8,893,756 and 105,510. Those are historical static archive
sizes with their recorded compiler, not current linked-binary size, resident
memory, timing or new stx_skia measurements. Dead stripping may already remove
unused objects from applications; source pruning can reduce maintenance/build
work without improving drawing speed.

The next measurement must count compiled source, linked code, allocations and
peak workspace as well as draw time. Preserve image differences and regressions.


## First implementation findings

The original macOS closure still compiled `SkImageGeneratorCG.cpp`, despite the
intended external codec policy. The candidate removes this ImageIO entry path,
generic codec/encoder infrastructure and Android codec adapters. Encoded input
is rejected at the retained compatibility entry point.

The coordinator requires layers, full affine transforms, sweep gradients,
additional blends, arbitrary paths, cubic/mipmap sampling and per-corner radii.
The first seven capabilities remain. The owner subsequently withdrew the
optional Skia backdrop capability: general image filters are now a removal target,
followed by their SkSL dependencies. First-party GUI.Forms effects own backdrop
and CSS filtering; blur masks, matrix color filters and shader masks remain.

The new ownership helper is used in an isolated consumer patch for decoded and
remapped pixel vectors. Gradient buffers are reused by the raster instance;
shader construction copies the stop values synchronously. Borrowed texture
snapshots still copy. Measurements and remaining gates are recorded under
`results/2026-10-25/`.
