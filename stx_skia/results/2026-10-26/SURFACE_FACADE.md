# CPU surface API conversion — 2026-10-26

The public SkSurface header and implementation now describe and implement the
retained CPU contract in house style. GPU recording-context/recorder queries,
backend-texture replacement, backend-access enums, semaphore waits and deferred
surface-characterization APIs are removed, together with their base virtual
stubs. Surface YUV420/YUVA420 reads are removed: their CPU implementation only
reported a null callback result. The internal surface kind now admits null and
raster surfaces only. These changes do not claim removal of every GPU-era
compatibility declaration elsewhere in the library.

CPU recorder access, normal pixel reads/writes, rescaling, snapshots, drawing,
compatible-surface factories and property propagation remain. The public header
states buffer extents, disjoint copy storage, ownership and borrowed-view
lifetimes. The retained historically named async read executes synchronously;
its documented callback receives one owned result or null before return.

The facade uses named implementation references, pixel views, owned results and
explicit failure exits. Clipping, copy-on-write selection and pixel arithmetic
are unchanged. The notification API still cannot report copy-on-write allocation
failure; conversion does not invent a stronger guarantee. Surface properties'
legacy fields remain a separate cleanup task. Original source/header copies
were already preserved in the isolated reference profile.

## Verification

The surface test adds compile-time checks that GPU context/recorder, semaphore,
characterization and YUV read interfaces exist in the original reference and are
absent in the candidate. Runtime tests add:

- Owned and wrapped 9×7 RGBA8888 surfaces with 52-byte rows.
- A 5×4 patterned source with 28-byte rows, copied at negative x offset while
  the drawing canvas has a translation and empty clip. Pixel writes remain
  independent of drawing transforms/clips.
- Reads at negative source offsets into an 11×9 image with 60-byte rows;
  independently computed expected bytes, untouched borders and row padding.
- Disjoint writes preserving generation identity; disjoint reads preserving
  destination storage; wrapped destination padding preservation.
- Full bitmap writes/readback and snapshot preservation through both partial
  and full writes; compatible-surface dimensions, invalid dimensions and drawing
  a source surface into an independently allocated target.

M4 macOS arm64, non-Apple Clang 23.1.2: candidate and original reference pass all
17 tests. Seven rendering artifacts compare byte-for-byte, including mask blur,
shader masks and color-filter/blend coverage. Release and Debug instrumented
suites report PASS. Previous diagnostic logs are preserved; detailed contents
were not read. A fresh relocated installed consumer passes.

All five changed C++ source/header/test files have zero spelling and lifetime
checker findings. Checks supplement ownership and behavioral review; they do
not establish conversion of unrelated imported implementation. No speed or
pixel-memory improvement is claimed for this API cleanup.

Commands, run sequentially from the repository:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/bin:/bin sh tools/build_native.sh candidate
```

The gate driver retained locally at `build/surface_facade_gate.sh` builds the
original reference, compares the seven artifacts, preserves prior instrumented
logs, runs Release and Debug suites and verifies a fresh relocated installation.
Records: `build/surface-facade-candidate.log`, `build/surface-facade-gates.log`.

Seven selected GUI.Forms tests also pass with pinned non-Apple LLVM 22.1.8,
covering prepared text, damage, group opacity, shadows, smoke, drawing and C API.
The authoritative GUI.Forms checkout is unchanged. Integration record:
`build/surface-facade-gui.log`; command:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 sh tools/verify_gui.sh
```

Native Linux/Windows CI on this change, broader conversion and the owner's
independent review remain separate gates.
