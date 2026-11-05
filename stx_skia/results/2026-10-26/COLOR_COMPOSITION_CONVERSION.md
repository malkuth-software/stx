# Color-filter composition and working-space conversion

Converted the public color-filter facade, scalar filter pipeline, composition,
working-format calculator/wrapper, color-space transform filter and filtered
shader wrapper. Reviewed their headers along with the implementation; unchanged
headers already use explicit field initialization in their constructors and
return owned references by value. Pre-conversion copies remain reference-only.

Child filters are retained by the composed owner. Stage construction explicitly
appends the inner filter before the outer filter and stops on failure. Failure
can leave partial stages, so the caller discards that pipeline. The scalar
pipeline declares its destination and context before the borrowing pipeline.
Working-space conversion records live in the arena through pipeline execution;
source color-space objects are needed during construction only.

The base-view helper rejects temporary owning handles. This caught the filtered
shader's implicit conversion from a base owner to a public owner: the shader
already has the required base type and now accesses it directly. No additional
reference-count operation is needed for that opacity query.

Working-format serialization spells the seven transfer-function coefficients
and nine row-major gamut coefficients in actual arrays. Deserialization maps
those arrays into their named fields and rejects failed reads before constructing
a wrapper. This preserves valid wire values while avoiding traversal across
separate struct fields or C-array rows. Default inherited coefficients are
initialized even when their corresponding override is absent. Color arithmetic,
clamping and the established child-only matrix/mode query contract are retained.

The dedicated fixture checks null composition, child retention, inner/outer
order, stop-on-failure and opacity propagation, scalar composition against
successive application, sRGB and linear working-space execution in scalar and
raster paths, all eight working-format override combinations and their wire
arrays, and filtered-shader alpha scaling at zero, half and full opacity. Scalar
and raster approximation paths are recorded separately rather than assumed
bit-identical to each other. Six shader pixels also match independent filter
application. A compile-time check rejects temporary base views.

M4 Mini, non-Apple LLVM, Release: candidate 53/53 and reference 52/52 pass.
The 27,008-byte color-composition artifact is byte-identical to the reference.
All nine existing output artifacts also match byte for byte. These observations
support this conversion; they do not establish performance gains or clear the
remaining raster implementation.

Commands follow AGENTS.md: sequential candidate, baseline and candidate-sanitize
profiles through m4build, then direct byte comparisons. House spelling and
lifetime checks report no findings in the converted files and new fixture.

The complete candidate instrumented suite returned PASS; its diagnostic file was
not read here. A fresh package install was relocated from
`build/color-filter-install-original` to `build/color-filter-install-relocated`.
The independent installed consumer compiled, linked and ran with Clang 23.1.2,
including a composed matrix check through the public installed headers. Its
build lives in `build/color-filter-installed-consumer` on the M4 mirror.
