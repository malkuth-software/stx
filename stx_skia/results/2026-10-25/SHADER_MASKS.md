# Shader-mask ownership and row copies — 2026-10-25

The retained CPU shader-mask implementation and public factory are converted.
The original source/header copies remain selected by the reference build.

## Changes

The local row-copy loop is replaced by the already-converted SkRectMemcpy kernel.
It forms only live row addresses, including source allocations ending at the last
live pixel rather than at the end of the padded final stride. Source dimensions,
row stride and extent arithmetic are checked before copying. Actual allocation
extent remains a caller precondition because SkMask has no allocation-length field.

An explicit mask allocation owner precedes bitmap/canvas borrowers. The result is
published only after those borrowers finish; failures preserve output metadata
and margin. Source and destination descriptors must be distinct. As required by
the base contract, destination storage is initially empty. Metadata-only sources
retain their allocation-free path. Source-in alpha multiplication and transform
order are unchanged. The image-filter adapter names its shader and blend owners.

This removes duplicate code and makes ownership explicit. No runtime speed gain
is claimed; this batch does not change the shader raster pipeline itself.

## Verification

M4 Mac mini, non-Apple Clang 23.1.2/C++20 for standalone profiles; the isolated
GUI.Forms fixture uses pinned LLVM 22.1.8.

- Eleven deterministic release tests pass in each profile.
- 576 shader-mask cases compare byte for byte: four alpha/gradient shaders,
  widths 1/7/16/33, heights 1/7, three origins, two paddings and three transforms.
  Shader-only FNV hash is 4976865620939446745.
- Zero/opaque constant shader expectations are independently checked. Metadata,
  image-filter adaptation and null factory behavior are checked. Candidate-only
  malformed stride and alias checks verify failure preservation.
- All five complete differential artifacts match, including the extended
  32,286,912-byte mask-filter.raw containing the original 240 scenes followed by shader cases.
- Whole Release and Debug instrumented suites report PASS. Previous logs are
  preserved; detailed diagnostics remain unread.
- Seven selected GUI.Forms integration tests pass. A freshly installed and
  relocated consumer builds and runs with consumer RTTI preserved.
- All four converted production/test files pass house-style spelling and
  lifetime checks.

Release and instrumented build logs are build/shader-mask-release.log and
build/shader-mask-instrumented-build.log. Native platform and broader integration
completion are separate gates; the full library remains under conversion.
