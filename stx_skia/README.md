# stx_skia

A CPU raster library for GUI.Forms, derived from Skia and narrowed to its drawing contract.

Paths, gradients, clipping, shadows, bitmap sampling and alpha compositing stay
inside the renderer. Image decoding and font shaping stay with the consumer:
GUI.Forms uses `stx_png`/`stx_zlib` and its CNG resource path for images, and
HarfBuzz/FreeType for text. This library receives pixels and coverage masks.

## Working implementation

The first CMake implementation builds a static library and an installable package.
It preserves the private Skia drawing vocabulary used by GUI.Forms. It is **not**
a complete upstream Skia distribution, and most retained upstream implementation
still awaits house-style conversion.

- CPU surfaces, paths, strokes, dashes, clipping and affine transforms.
- Linear, radial and sweep gradients; nearest, linear, cubic and mipmap sampling.
- Layers, group opacity, blend modes, rounded corners, blur masks, shader masks and matrix color filters.
- RGBA/BGRA pixels and A8 masks, with explicit transfer of owned pixel storage.
- No compiled GPU backend, image decoder/encoder or platform font backend.

The candidate removes generic codec scaffolding, Apple's encoded-image generator,
the compressed-texture decoder, unused effects and wider CPU-dispatch objects. Excluded GPU implementations,
unused modules and upstream infrastructure have also been removed from the tree.
All 17 general image-filter factory implementations and their public factory
header are removed from the candidate. GUI.Forms owns CSS and backdrop effects.
The core image-filter graph, cache, paint field and device/image entry points are
also removed. Ordinary layers compose devices directly. SkSL/runtime effects, programmable
meshes and shader-language capability APIs are also removed. The canvas no longer
exposes text drawing or owns glyph-run scratch; external A8 glyph-mask composition
remains. Picture recording/playback/capture, picture-backed images and shaders,
font parsing/rasterization, text blobs, typefaces and strike caches are removed.
The retained CPU pipeline has no font engine dependency. Surface APIs expose CPU
pixel ownership, snapshots and reads; unsupported GPU texture/context/semaphore
interfaces and CPU-unimplemented surface YUV reads are removed. Deferred CPU
generators remain; unused planar YUV generation/cache and GPU listeners are removed.
Image APIs exclude backend context pointers, texture/protected-content operations
and unsupported YUV reads; existing null-context pixel reads remain compatible.

On the initial recorded M4/macOS build, the candidate passed seven selected GUI.Forms
renderer tests and produced byte-identical output across 24 baseline comparison
cases. Its static archive is 6.1% smaller, with essentially unchanged rendering
time in that small workload. Pixel ownership transfer removes a full buffer copy.
See the [measurement record](results/2026-10-25/README.md) for exact workloads,
times, negative results and limits. Cross-platform consumer gates and the owner's
independent review remain outstanding.

## Build and consume

Use non-Apple Clang, CMake 3.24 or later and Ninja. No dependency download is needed
for the standalone library.

```sh
cmake -S . -B build/candidate -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/candidate
ctest --test-dir build/candidate --output-on-failure
cmake --install build/candidate --prefix /your/install/prefix
```

```cmake
find_package(stx_skia CONFIG REQUIRED)
target_link_libraries(your_private_renderer PRIVATE stx_skia::stx_skia)
```

The exported target requires C++20. Skia classes do not supply RTTI; translation
units deriving from them must respect that boundary. Consumer RTTI for its own
types remains available. Instrumented builds exclude RTTI-dependent vptr checks
at this interface. Windows Clang consumers inherit the Microsoft syntax extension
required by upstream headers. Keep Skia types behind the application's
private renderer adapter. Installed headers include upstream declarations beyond
the admitted subset; their presence does not promise those facilities are linked.
`SkImages::DeferredFromEncodedData` rejects encoded input. Decode externally.

`stx_skia::take_pixel_data` transfers a `std::vector<std::byte>` allocation into
immutable `SkData` and leaves the source empty. Callers must surrender writable
aliases. The returned data, shared subsets and images retain the storage until
its final release. Skia's existing fail-fast allocation policy still applies.

`tools/build_native.sh candidate` builds and tests this tree and writes its
corpora; `baseline` builds the reference profile, whose library comes from a
checkout of `malkuth-software/stx_skia` at the pin (`STX_SKIA_REFERENCE_ROOT`)
and whose tests come from this tree; `tools/compare_profiles.py` then requires
the two profiles' corpora to match except the intentional differences listed in
`tests/intentional_differences.txt`. `candidate-sanitize` runs the suite under
AddressSanitizer and UndefinedBehaviorSanitizer with diagnostics on the console.
`tools/verify_installed.sh` checks a relocated installation. The retained GN
profiles in the pinned repository describe the original intake, not this build.
`candidate-scalar` builds this tree with no SIMD, and `tools/compare_isa.py`
checks the shipped SSE2 or NEON profile's pixels against it within the bounds in
`tests/isa_bounds.txt`; `tools/run_recorded_workloads.py` runs every timing tool
(docs/ISA_PROFILES.md).

## In GUI.Forms

This directory is GUI.Forms' `stx_skia/`. Built as a subdirectory it declares no
project, builds its tests when `GUI_FORMS_BUILD_TESTS` is on (`STX_SKIA_TESTS`),
names them `stx_skia_*`, and installs nothing unless `STX_SKIA_INSTALL` is set.
The `stx-skia` workflow builds both profiles on Linux x86-64, Linux arm64 and
Windows when this directory changes; the house-style job checks it against
`house_style_allowance.txt`. The repository `malkuth-software/stx_skia`, from
which it was taken at the pin in PROVENANCE.md, keeps the reference sources
(`baseline/`), the GN profiles and the original integration patch.

## Design and provenance

- [Consumer scope and pruning investigation](docs/INTAKE.md)
- [Coordinator requirements, including future material capabilities](docs/COORDINATOR_REQUIREMENTS.md)
- [Implementation sequence and remaining work](docs/PLAN.md)
- [Full conversion ledger](docs/CONVERSION.md)
- [Instruction-set profiles, the scalar reference and the recorded workloads](docs/ISA_PROFILES.md)
- [Programming house style](../planning/PROGRAMMING_HOUSE_STYLE.md)
- [Source identities and modifications](PROVENANCE.md)
- The original source inventory and removed-source manifest: `baseline/inventory.json`
  and `baseline/pruned-source-files.json` in the pinned repository

Skia is pinned to `2a9b593bab4b2fd019fa494c8d401ff1fab0b883` (`chrome/m152`).
Its BSD-style license remains in [skia/LICENSE](skia/LICENSE), also copied to
[LICENSE-Skia](LICENSE-Skia). Component notices remain with retained sources.
Original additions and GUI.Forms-derived material use [LICENSE](LICENSE).
Refactoring does not replace upstream copyright or component licenses.
