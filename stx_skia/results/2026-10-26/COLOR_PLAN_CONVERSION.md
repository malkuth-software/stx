# Color-conversion plan and remaining color filters

Converted SkColorSpaceXformSteps and the blend-mode and Gaussian color filters.
The conversion plan owns initialized transfer functions and fixed-size gamut/OOTF
arrays. Scalar conversion keeps the existing binary32 operations and ordering;
the finite-reciprocal test is explicit instead of an anonymous callback. Gamut
weights are accumulated locally before publication. Debug hash-collision checks
compare named transfer coefficients rather than indexing across struct members.

Raster stages borrow coefficient storage. The pipeline overload now requires an
lvalue plan and documents its lifetime through execution; a deleted rvalue
overload rejects a temporary plan at compile time. Scalar conversion still admits
temporaries because it finishes before the call returns. Source color spaces are
not retained by the completed plan. Disabled coefficient arrays are initialized,
and their flags still determine which values are consumed.

The blend filter uses explicit owner transfers, a named color-space plan and
initialized deserialization values. Existing mode simplification and rounding
remain. The Gaussian coverage filter retains its raster approximation and is
still used by the shadow implementation. Original versions remain in the
reference build; headers for both filters were reviewed with their constructors.

The new color_plan_contract fixture checks default/no-op plans, alpha-only plans,
opaque-destination behavior, zero and nonfinite alpha values, 576 combinations of
eight color spaces and three source/destination alpha types, and 17-pixel raster
batches. Color spaces include null defaults, sRGB, linear sRGB, Display P3,
linear Rec.2020, PQ, and HLG with system gamma 1.2 and 1.0. Scalar and raster
results are compared separately with their reference paths. Filter coverage
includes all 29 blend modes, two color spaces, three constant-alpha values and
33 Gaussian-ramp samples: 2,121 filtered samples in total. Tests use flat RGBA
buffers so raster pointer arithmetic stays within one live array.

M4 Mini, non-Apple LLVM, Release: candidate 54/54; reference 53/53. The
351,640-byte color-plan output and all ten other generated output files match
byte for byte. The complete instrumented candidate suite returns PASS; detailed
diagnostics were not read. House spelling and lifetime checks report no findings
in the converted files and fixture. This is conversion correctness evidence;
no speed claim is made from the test runtime.

The native CI workflow now compares all three color output files on Linux and
Windows as well as the existing eight artifacts. Both shell bodies use set -eu
so a failed comparison cannot be hidden by a later successful command.

Verification uses the sequential candidate, baseline and candidate-sanitize
commands in AGENTS.md. The debug-only code is additionally syntax-compiled using
the candidate compile command with -DNDEBUG replaced by -DSK_DEBUG and
-fsyntax-only; this is a compile check, not a debug runtime suite.
