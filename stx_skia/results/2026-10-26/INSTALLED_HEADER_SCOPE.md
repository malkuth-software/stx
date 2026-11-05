# Installed CPU header scope

The package had installed headers for excluded SVG, codec, encoding, Android,
platform image hosts, Chromium/GPU adapters, and effects whose implementation
was pruned. A consumer could include those headers despite the CPU-only package
being unable to provide the advertised functionality.

The install rules now exclude those directories and named removed effects.
`skcms.h` retains only its required public companion header, rather than the
module's internal implementation headers. The installed consumer checks that
representative excluded headers are absent and that the public surface
notification returns a checked result.

`SkAnySubclass` is also omitted from the installed CPU package: its only
current uses are in excluded GPU code. The private-header review recorded the
template's alignment and base-subobject assumptions should GPU support ever
be admitted.

On the M4 Mini, a fresh install contained 138 header files versus 210 in the
preceding install. The fresh installed consumer compiled, linked and ran. The
candidate and pinned-reference release suites each passed 49/49 tests; the
candidate sanitizer runner reported `PASS`. Rendering artifacts matched in
`tools/measure.py` (`PASS`).

Every installed Skia header under `include/core`, `include/effects`,
`include/utils`, `include/pathops`, `include/config`, and `include/private` was
then compiled as its own C++20 translation unit against the fresh prefix:
91 public and 44 private headers, zero compile failures. This checks include
closure and parseability, not whether every declaration links or behaves as
specified.

This removes misleading installed declarations. It does not establish that
every remaining public header has a complete implementation; that is part of
the continuing production-header review.
