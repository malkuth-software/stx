# Working-space shader context corrections

The public shader contract says a null input color space inherits the destination.
The wrapper resolved that input for its conversion plans, then gave its child
fInputSpace.get(), which was still null. A constant sRGB shader on a linear
surface consequently produced 0.5 for a red component that should be about
0.213993073. A reproducer failed before the repair and returned the same red
component as the unwrapped shader after passing the resolved input to the child.

The wrapper also omitted the caller's device offset when creating its child
stage record. That offset carries tile placement to clip shaders. The child now
receives the original offset. A stage observer checks nonzero positive/negative
offsets and preserves the existing stop-on-child-failure behavior.

These are context-propagation corrections, separate from the following house
style rewrite. The reference fixture explicitly reproduces the old color-space error;
it does not count that as correct rendering. The original reference stage record
predates the device-offset field, so offset forwarding is a candidate-only check. The byte-comparison artifact holds
216 explicit-input-space cases that should remain unchanged, with inherited and
explicitly equal output spaces compared within each case.

## Following house-style conversion

The constant-color shader and working-color-space wrapper now use explicit types,
initialized values, named color-conversion plans and owner transfers. A named
WorkingSpace record replaces the positional tuple and structured binding. The
wrapper copies the incoming stage record and overrides only the resolved color
space and paint color, retaining tile placement and bounds. Source color-space
owners stay live while child stages are constructed; arena-owned conversion
coefficients stay live through pipeline execution.

The constant-color view rejects temporary shader objects. Serialization holds
its data owner in a named local through the write. Existing baseline copies and
reference selections were preserved. These changes retain the corrections in
the preceding commit rather than folding them into an unexplained pixel change.

Candidate release verification passes 55/55 tests. House spelling and lifetime
checks report no findings across the four converted files and the fixture.

Final M4 release verification: candidate 55/55, reference 54/54. All twelve
common-output artifacts match byte for byte, including the new 3,456-byte shader
working-space output. The full candidate instrumented suite returned PASS; its
diagnostic file was not read. These checks establish the scoped corrections and
conversion, not a completed shader/raster conversion or a new speed result.

Commands are the candidate, baseline and candidate-sanitize profiles of
`tools/build_native.sh` through m4build as documented in AGENTS.md. The native CI
workflow compares shader-working-space.raw on every existing platform job.
