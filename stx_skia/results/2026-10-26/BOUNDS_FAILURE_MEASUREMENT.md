# Release failure path of the collection bounds helpers

## What Release actually does

`sk_collection_check_bounds`, `sk_collection_check_length` and `sk_collection_not_empty`
(used by `SkSpan`, `SkTDArray`, `AutoTArray` and other containers) print a message and
abort in Debug. In Release they execute `SkUNREACHABLE`, which `SkAssert.h` defines as
`__builtin_trap()` for Clang and GCC and `__fastfail` for MSVC. That definition is unchanged
since intake 490c9bd, and nothing in the build overrides it. So an out-of-range index
already **terminates** in Release; it is not undefined behavior. The code-integrity scan
record said otherwise; it is corrected.

A probe linked against the Release candidate library confirmed it: in-range
`SkTDArray` and `SkSpan` reads returned their values, and out-of-range reads ended with
SIGTRAP (exit status 133).

## Measurement

`tools/measure_bounds.sh` builds three Release candidate trees that differ only in a
force-included header defining `SkUNREACHABLE`, then `tools/measure_bounds.py` times them,
rotating the order across nine trials after a warm-up and checking that every corpus hash
is identical across variants:

- **trap**: the current definition;
- **unreachable**: `__builtin_unreachable()`, so the optimizer may drop the check (the cost
  floor, and the "undefined" behavior the scan record wrongly described);
- **abort**: `SK_ABORT("unreachable")`, a logged message followed by abort.

The header also changes SkUNREACHABLE's 38 other uses in the built areas, all
switch-fallback or failure paths.

M4 Mini, macOS arm64, non-Apple Clang, Release. Medians in seconds (render: draw time only;
others: process wall time without file output), with change against trap:

| Workload | trap | unreachable | abort |
| --- | --- | --- | --- |
| render draw (24 cases) | 0.0079 | 0.0079 (−0.2%) | 0.0077 (−2.9%) |
| path construction corpus | 0.0912 | 0.0921 (+0.9%) | 0.0926 (+1.5%) |
| curve geometry corpus | 0.0296 | 0.0299 (+1.0%) | 0.0299 (+1.2%) |
| contour measure corpus | 0.0210 | 0.0207 (−1.4%) | 0.0207 (−1.6%) |
| stroke geometry corpus | 0.0861 | 0.0872 (+1.3%) | 0.0865 (+0.5%) |
| dash effects corpus | 0.1537 | 0.1515 (−1.4%) | 0.1638 (+6.6%) |
| `SkTDArray` benchmark | 0.0185 | 0.0190 (+2.9%) | 0.0195 (+5.4%) |
| `TArray` construction benchmark | 0.0315 | 0.0355 (+12.6%) | 0.0344 (+9.1%) |

For every workload the min–max ranges of the three variants overlap, and removing the
check entirely (unreachable) was not faster than trapping on any workload beyond that
spread. No runtime difference between the variants is resolved by this measurement; with
runs this short (8–160 ms) a difference below roughly ±5% could not be resolved, and none
of these numbers is a speed claim.

Code size of `libstx_skia.a`:

| | trap | unreachable | abort |
| --- | --- | --- | --- |
| text bytes | 1,023,483 | 1,020,915 (−2,568) | 1,037,203 (+13,720, +1.3%) |
| archive bytes | 2,204,352 | 2,201,752 | 2,236,400 |
| `brk` instructions | 211 | 58 | 57 |

The current traps cost about 2.5 KB of text over no check at all. A logged abort at each
failure site would add about 13.7 KB.

## Proposal

Keep the current Release definition (trap). It already gives the property the
code-integrity rules ask for: an out-of-range access stops before memory is read or
written. It is the smallest variant that keeps the check, and its runtime cost cannot be
told apart from no check here. Switching to `SK_ABORT` would add only a message, and only
in Release: Debug builds already print the index and size. The cost is about 1.3% more
library text plus format strings, with no safety gain. Do not switch to
`__builtin_unreachable()`.

If Release crash reports need the index, a better use of that cost would be one shared
out-of-line, cold, `noreturn` reporter: one function, not a message at every call site.
That would need its own measurement; it is not done here.
