# Clip stack and clip-stack device: removal — 2026-10-27

Scope (relative to `skia/`): `src/core/SkClipStack.cpp/.h` and `src/core/SkClipStackDevice.cpp/.h`
(1,678 lines; 162 excluded constructs in the AST inventory, almost all arrows). They were listed
as still to be converted. They are removed instead: the candidate compiled them, but nothing
used them.

## Evidence

- No compiled source or header in the candidate names `SkClipStack` or `SkClipStackDevice`,
  apart from the four files themselves. The only other references are in backends the library
  does not admit: the PDF, SVG and XPS devices and graphic state, and
  `src/utils/SkClipStackUtils`, which the build never compiled.
- None of the reference profile's original sources under `baseline/skia` references them.
- GUI.Forms does not use them. The headers are private to `src/core`, so they are not part of
  the public API.
- The CPU canvas clips through `SkRasterClipStack` and `SkRasterClip`, which are converted and
  covered by the region-clip, canvas-device and render corpora.

The removal is proved by building, not by the source search alone:
- the candidate builds without the files and passes 78/78;
- the reference, which also no longer compiles them, passes 77/77;
- all 39 shared artifacts are byte-identical, and only the intentional
  `image-sampling-corrected.raw`, `m44-corrected.raw` and `matrix-invert-small.raw` differ;
- instrumented suite: PASS.

## Change

- The four files move unchanged, with their notices, to `baseline/skia/src/core/`, following the
  earlier facility removals.
- `cmake/sources.cmake` no longer lists the two implementations.
- `tests/canvas_device.cpp` fails to compile in the candidate if either header becomes reachable
  again.
- The upstream `skia/tests/ClipStackTest.cpp` stays in place as preserved upstream test source.
  It is not built.

The AST inventory falls from 1,839 excluded constructs in 91 files to 1,677 in 88.

## Not done

- No defects were fixed in the removed code; it has no behavior left to fix in this library.
- The GUI.Forms consumer gates (`verify_installed.sh`, `verify_gui.sh`) were not run, as before.
