# Wrapper canvases and canvas-state helpers: removal — 2026-10-27

Scope (relative to `skia/`): `SkNWayCanvas`, `SkPaintFilterCanvas`, `SkNullCanvas` and
`SkCanvasStack`, as requested. The use check pulled in two compiled helpers: `SkCanvasStateUtils`
(the only user of `SkCanvasStack`) and `SkAndroidFrameworkUtils` (the only user of
`SkPaintFilterCanvas`).

## Use check

| Facility | Users before this change | Outcome |
|---|---|---|
| `SkNWayCanvas` (public, installed) | Base of the other three wrappers only | Removed |
| `SkPaintFilterCanvas` (public, installed) | `SkAndroidFrameworkUtils` only | Removed |
| `SkNullCanvas` / `SkMakeNullCanvas` (public, installed) | None | Removed |
| `SkCanvasStack` (private) | `SkCanvasStateUtils` only | Removed |
| `SkCanvasStateUtils` (public, installed) | None | Removed |
| `SkAndroidFrameworkUtils` (header in `include/android`, not installed) | `SkSwizzler`, under `SK_BUILD_FOR_ANDROID_FRAMEWORK` only, which this library never defines | Removed |

GUI.Forms uses none of them, and no library test did. The base of these wrappers, `SkNoDrawCanvas`,
stays: `SkCanvas` and the null surface use it.

## Change

- `SkNullCanvas`, `SkCanvasStack`, `SkCanvasStateUtils` and `SkAndroidFrameworkUtils` (sources
  and headers) were unchanged from intake. They move unchanged, with their notices, to
  `baseline/skia`.
- The candidate copies of `SkNWayCanvas` and `SkPaintFilterCanvas` are deleted. Their intake
  originals were already in `baseline/skia`, and the reference build keeps compiling them.
- `cmake/sources.cmake` drops the six implementations.
- `SkCanvas.h` loses the declarations that named the removed classes:
  - the `SkPaintFilterCanvas` forward declaration;
  - the `SkAndroidFrameworkUtils`, `SkNWayCanvas` and `SkCanvasStateUtils` friendships;
  - the `internal_private_asPaintFilterCanvas` virtual;
  - a deprecation note pointing at `SkAndroidFrameworkUtils`.
- The virtual-enforcer comment no longer cites `SkNWayCanvas`.
- `tests/canvas_device.cpp` fails to compile in the candidate if any of the headers is reachable,
  and the installed-consumer test checks the four installed ones.

Nothing remained to convert: every class in scope was unused. The removal deletes 155 excluded
constructs.

## Verification

**Gates.** All were run sequentially:
- Release candidate: 82/82;
- reference: 77/77;
- all 40 shared artifacts match, and only the intentional `image-sampling-corrected.raw`,
  `m44-corrected.raw`, `matrix-invert-small.raw` and `raster-clip-stack-replaced.raw` differ;
- instrumented suite: PASS.

**Inventory.** The AST inventory falls from 1,657 excluded constructs in 82 files to 1,502 in 76.

The installed-consumer and GUI.Forms gates were not run, as before.

## Open: Android canvas hooks

SkCanvas still carries Android-framework hooks that no compiled code calls:
- `androidFramework_setDeviceClipRestriction`, whose clip-restriction state is threaded through
  restore and clip reset;
- `androidFramework_isClipAA`, whose last caller was `SkCanvasStateUtils`;
- `temporary_internal_getRgnClip`;
- the device virtuals `isClipAntiAliased` and `android_utils_clipAsRgn`, which only those hooks
  call.

Removing them changes SkCanvas internals, so it is left as its own unit.
