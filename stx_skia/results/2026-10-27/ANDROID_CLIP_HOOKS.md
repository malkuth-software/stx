# SkCanvas Android clip hooks: kept, converted and fixed — 2026-10-27

Scope (relative to `skia/`): `SkCanvas::androidFramework_setDeviceClipRestriction`, the clip
restriction state and its use in `internalRestore` and `onResetClip`,
`SkCanvas::androidFramework_isClipAA`, `SkCanvas::temporary_internal_getRgnClip`, and the device
virtuals behind them (`SkDevice::isClipAntiAliased`, `SkDevice::android_utils_clipAsRgn`,
`SkDevice::android_utils_clipWithStencil`).

The unit was requested as a removal. The owner then said the Android framework support may be
needed, so the hooks stay and are converted and fixed instead. Nothing in the library or
GUI.Forms calls them today: `SkAndroidFrameworkUtils`, their former in-library user, was removed
on 2026-10-27 and remains in `baseline/skia`.

## Evidence before any library change

`tests/android_clip.cpp` (commit c733af19) runs random sequences of saves, layers, restores,
rect, rounded-rect, path and region clips under random transforms, clip resets, device clip
restrictions and fills. The canvases are raster surfaces of 64x48, 1x1 and 20000x8 (the last
wide enough to disable antialiased clipping) and a 64x48 no-draw canvas. After every step the
corpus records:
- the save count;
- the device clip bounds;
- whether the clip is empty or a rectangle;
- `androidFramework_isClipAA`, reached through the `skiatest::TestCanvas` friend;
- every rectangle of `temporary_internal_getRgnClip`.

Each run ends with a hash of its pixels. Restrictions are set only where the contract allows:
on the root layer, with no restriction active. Some runs restrict the root save level.

Before any library change both builds wrote identical `android-clip.raw`: 5,808 cases,
322,088 bytes, hash 9788295869597394689. Every later commit leaves that file unchanged.

## Changes

1. **Prune (45a70157).** `android_utils_clipWithStencil` is removed from `SkDevice`. Only the
   Ganesh and Graphite devices overrode it, and only `SkAndroidFrameworkUtils` called it. No GPU
   backend is admitted. The test checks it is absent from the candidate and present in the
   reference.
2. **Mechanical rewrite (7d5530fb).** `temporary_internal_getRgnClip` takes the top device by reference,
   since a canvas always has one, and tests the origin with explicit comparisons instead of a
   bitwise or. `android-clip.raw` and every other shared artifact were unchanged.
3. **Fixes (this commit):**
   - **The restriction state can no longer be inconsistent.** It was a rectangle and a save
     count that had to be kept in step (empty and -1 meaning none). It is now one
     `std::optional<ClipRestriction>`.
   - **Resetting a no-draw canvas ends a root-level restriction.** `resetForNextPicture`
     restores to the root save level, which never ends a restriction set there. It then
     resets the device clip but kept the restriction. Afterwards a clip reset still restricted
     to the old rectangle, possibly outside the new bounds. A new restriction was silently
     ignored in Release, so drawing could escape it.
   - **Release builds stop on restriction contract violations.** Upstream checked both
     preconditions only in Debug. A second restriction while one is active was ignored, so
     drawing outside the new rectangle succeeded. A restriction set inside a layer applied
     only to that layer, and a clip reset in the layer escaped it. Both now stop. An empty
     rectangle still does nothing.
   - **A null region stops.** `temporary_internal_getRgnClip(nullptr)` dereferenced null; it
     now stops in Release. The pointer signature is kept for source compatibility with the
     Android framework.
   - **The documentation matches the code.** The restriction header said an empty rectangle
     disables the restriction and that the restriction affects only future clips. In fact an
     empty rectangle does nothing, and the restriction intersects the clip immediately. It
     ends on restore past its save level or on a no-draw reset, and has the preconditions
     above. The `androidFramework_isClipAA` comment said "any active layer"; it reads the top
     layer only. The region comment now says the region may be larger than the clip: for
     antialiased clips, layers without pixels, and shader clips.

## Not changed

- **The unaligned-layer branch of `temporary_internal_getRgnClip` is unreachable here.** It
  returns an empty region when the top layer is not pixel-aligned with the canvas. Every
  device here is pixel-aligned: `internalSaveLayer` only composes the parent's device-to-global
  matrix with an integer translation.
- **Clip resets inside a layer still ignore the restriction.** This is correct for a
  restriction set on the root layer: the layer is clipped by the root clip when it is restored.

## Verification

- **Builds:**
  - candidate: 86/86, including three new stop tests (`restrict-twice`, `restrict-in-layer`,
    `null-region`);
  - reference: 78/78;
  - instrumented suite: PASS.
- **`android-clip.raw`:** identical in both builds, with the same hash as before the change.
- **`android-clip-reset.raw`:** meant to differ. It runs the same sequences on one no-draw
  canvas that is reset before each run. The candidate checks that those bytes equal the same
  runs on a new canvas each time, so a reset canvas behaves as a new one. The reference differs
  (hash 18404195660798060392 against the candidate's 16232342364556655238).
- **Other artifacts:** all other shared artifacts are identical; the four earlier intentional
  differences are unchanged.
- **Not run:** the installed-consumer and GUI.Forms gates. The AST inventory was not
  refreshed for this unit.
