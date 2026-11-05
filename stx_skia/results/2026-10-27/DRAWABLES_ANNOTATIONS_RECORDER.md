# Drawables, annotations and the CPU recorder — 2026-10-27

Scope (relative to `skia/`): `SkDrawable`, `SkAnnotation` (with `SkAnnotationKeys.h`), the
`SkRecorder`/`skcpu::Recorder`/`skcpu::Context` classes and their implementations, and the
entry points that reach them. The first step was to check which are actually used:

| Facility | Consumer use | Library use | Outcome |
|---|---|---|---|
| Drawables | None: GUI.Forms includes neither header nor calls `drawDrawable` | `SkCanvas::drawDrawable` and the wrapper canvases only | Removed |
| Annotations | None | `SkCanvas::drawAnnotation`; the CPU device's handler was empty; only the excluded PDF device and the PDF-disabled stub used them | Removed, with the stub |
| CPU recorder and context | Indirect, through surfaces and images | Draws, the bitmap device, raster surfaces, raster and lazy images, the blur mask filter | Kept, pruned, converted and fixed |

The picture recorder and recorded drawables were already removed in earlier work.

There are four commits:
- 849652d8: remove drawables, annotations and the PDF stub;
- 589e02bc: prune the recorder;
- 6e195d33: mechanical conversion;
- the fix commit that adds this record.

## Removal (849652d8)

- `SkCanvas` loses `drawDrawable` (both forms), `drawAnnotation` (both forms) and their
  virtuals.
- `SkDevice`, `SkCanvasVirtualEnforcer`, `SkNoDrawCanvas`, `SkNWayCanvas` and
  `SkPaintFilterCanvas` lose their overrides, and `SkFlattenable` its drawable type. The type
  values are explicit, so the others keep their numbers.
- The candidate copies of `SkDrawable.h/.cpp` are deleted; the reference already compiles the
  intake originals in `baseline/skia`.
- `SkAnnotation.h/.cpp`, `SkAnnotationKeys.h` and `src/pdf/SkDocument_PDF_None.cpp` move
  unchanged to `baseline/skia`. The stub's `SkPDF::MakeDocument` returned null and its
  `SetNodeId` only annotated, and `include/docs` was already excluded from installation.
- `tests/canvas_device.cpp` asserts that the methods and headers are absent from the candidate
  and present in the reference, so the check is known to detect them. The installed-consumer
  test checks the same; it was not run, as before.

## Recorder prune (589e02bc)

- `SkRecorder::Type` loses `kGanesh` and `kGraphite`; no GPU backend is admitted.
- Each `RecorderImpl` held a raw pointer to the context that made it, and `SkDraw` copied it.
  Nothing read it, and it would have dangled once the context was destroyed. Both fields and
  the copy are removed.
- The two `SkDraw` comments that planned to take the mask cache from that pointer now state
  what their null cache does: it selects the process-wide mask cache.
- The intake `SkCPUContext` and `SkCPURecorderImpl` files now serve the reference profile,
  whose original device code still reads the pointer.

## Mechanical conversion (6e195d33)

- `AsRecorder` dereferences explicitly, tests null explicitly and names its result.
- The process-wide recorder is reached without an arrow, and a comment says it lives until exit.
- The recorder header's includes now come before its declarations.

## Defect fixed

**Recorders and contexts were downcast without a guarantee of their type.** The library makes
three unchecked downcasts:
- `AsRecorder` turns any `SkRecorder` that reports `Type::kCPU` into an `skcpu::Recorder`;
- `asRRI` turns any `skcpu::Recorder` into `RecorderImpl`, including `asRRI(this)` inside
  `Recorder::makeBitmapSurface`;
- `Context::makeRecorder` turns `this` into `ContextImpl`.

All three base classes are public, and their constructors could be reached by classes derived
outside the library (`SkRecorder` and `skcpu::Recorder` had public constructors, `skcpu::Context`
a protected one). Images take an `SkRecorder*` from the public API and call `cpuRecorder()` and
`makeBitmapSurface()` on it. So a caller-defined subclass would have reached an invalid
downcast, which is undefined behaviour.

Each class's constructor is now private, and its one admitted subclass is a friend:
- `skcpu::Recorder` for `SkRecorder`;
- `RecorderImpl` for `skcpu::Recorder`;
- `ContextImpl` for `skcpu::Context`.

The library's own classes are the only ones that can exist, so every cast is valid by
construction. `tests/surface.cpp` defines a concrete foreign subclass of each and asserts that
the candidate cannot construct them. The reference can construct the two that are concrete
there; its intake `SkRecorder` has further pure virtual functions.

## Verification

**Gates.** All were run sequentially at the fix commit:
- Release candidate: 82/82;
- reference: 77/77;
- all 40 shared artifacts match, and only the intentional `image-sampling-corrected.raw`,
  `m44-corrected.raw`, `matrix-invert-small.raw` and `raster-clip-stack-replaced.raw` differ;
- instrumented suite: PASS.

The prune and mechanical commits each built and passed in both profiles.

**Inventory.** The AST inventory falls from 1,677 excluded constructs in 88 files to 1,657 in 82.
The lexical checker reports nothing for the recorder, context and surface test sources.

## Open

- `SkNWayCanvas` and `SkPaintFilterCanvas` (84 findings) lost only their drawable and annotation
  overrides. They are reached through `SkNullCanvas`, `SkCanvasStack` and Android-only code, so
  they look like the next candidates for a use check.
- The process-wide recorder and context are intentionally leaked singletons, as upstream; no
  ownership change was made.
- `SkDocument` itself is still compiled; this work removed only the PDF-disabled stub.
