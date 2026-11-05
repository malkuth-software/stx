# Shared shader base and matrix state

The shared CPU shader base now names matrix composition, optional inverses,
context construction, ownership transfer during deserialization, and wrapper
creation. Matrix state starts initialized; gradient query records initialize
all fields, including the type-specific points and radii. Borrowed base views
reject temporary shared owners. Contexts explicitly borrow their shader and
reject construction directly from a temporary shader object.

Matrix application retains its numerical sequence: combine pending transforms,
invert, apply the post-inverse transform, restore tile device coordinates if
needed, then emit the seed and matrix stages. Inversion failure occurs before
pipeline mutation. A consumed matrix record retains the total matrix and validity
flag for later sampling decisions, while clearing pending work.

The two fragment-processor-only helpers (`applyForFragmentProcessor`, `applied`)
are removed. Source inspection found no callers in retained Skia sources or tests;
archive inspection subsequently found zero definitions in the candidate and the
two original definitions in the reference. The legacy CPU context path remains.
Existing baseline source/header snapshots were preserved unchanged.

## Verification

M4 mirror, non-Apple LLVM, sequential commands from AGENTS.md:

- Candidate Release: 56/56 tests PASS.
- Reference Release: 55/55 tests PASS.
- Complete deterministic instrumented candidate suite: PASS; diagnostics unread.
- All thirteen output artifacts match byte for byte, including the 35,568-byte
  coordinate fixture added in the preceding conversion.
- House spelling and lifetime checks: zero findings in both production files,
  the coordinate fixture and the updated installed consumer.

The coordinate fixture additionally checks one-time coordinate seeding, no stage
on an already-consumed identity, total matrix/inverse preservation, invalid total
matrix propagation, and unchanged stage list/count after singular inversion.
Compile-time checks reject temporary shader owners and confirm removal of the
fragment-processor helper. Existing render, sampling and tiled-clip fixtures
exercise the shared matrix path through actual drawing.

The relocated package fixture now queries a wrapped image shader's borrowed image,
local matrix and tile modes, and draws that shader through a composed color filter.
Fresh installation directories are `build/shader-base-install-original` (moved)
and `build/shader-base-install-relocated`; consumer output is
`build/shader-base-installed-consumer`. This verifies the public ref-qualified
image query links against the installed archive rather than relying on source-tree
headers. Installation output is retained in `build/shader-base-install.log`.

The base and local-matrix translation units are also syntax-checked using their
candidate compile commands, replacing `-DNDEBUG` with `-DSK_DEBUG` and enabling
`SK_ENABLE_LEGACY_SHADERCONTEXT`. This is compile evidence for the conditional
path, not a separate legacy/debug runtime suite.

No drawing speed claim follows from this conversion. Image/gradient sampling,
raster kernels and the broader canvas/geometry conversion remain active work.
