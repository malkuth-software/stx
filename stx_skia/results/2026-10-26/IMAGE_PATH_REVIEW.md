# CPU image path review

The focused Codex Security scan `e0081634-f04e-4bd3-bc10-5cb76475997d`
reviewed all 20 tracked files under `skia/src/image` at revision
`9df5e32c88d95b7602f3884184d5a72f437155e0`. It reported two low-severity
failure paths. The scan was static; it did not establish a route from an
untrusted image file to either path.

1. Owned raster snapshots can share backing with their source surface. If
   copy-on-write allocation fails, `aboutToDraw` returns false, but the public
   `notifyContentWillChange` discarded it. An external writer had no signal
   to stop before changing backing still shared with a snapshot. The method
   now returns a checked boolean, and its contract requires obtaining the
   writable pixel view after success.
2. An externally backed immutable image can exceed the byte limit of
   `RasterFromPixmap`. Rescaling a small part installed a view of the entire
   image, ignored temporary-image rejection, and dereferenced null. Both
   temporary-image paths now report failure through the null-result callback.

The first regression checks an external write after successful snapshot
separation. The second uses a virtually mapped image just above the temporary
factory's 2 GiB limit and checks that rescale calls back once with null.

Verification on the M4 Mini:

- `sh tools/build_native.sh candidate`: 49/49 tests passed; render artifact
  hashes stayed unchanged.
- `sh tools/build_native.sh candidate-sanitize`: `PASS`.
- House-style spelling check over the five touched C++ files: 0 findings.
- Lifetime-rule check over the same files: 0 findings.

This is a scoped review and repair. The remaining CPU library and GUI.Forms
integration still require their own review and gates.

The pinned reference still declares `notifyContentWillChange` as `void`. The
shared surface test now calls the reference API without expecting a result and
checks the candidate Boolean. Both release suites pass 49/49 tests; the
candidate sanitizer runner reports `PASS`. `tools/measure.py` reports identical
render artifacts and `PASS`. The fresh installed consumer compiles and runs
against the candidate package.
