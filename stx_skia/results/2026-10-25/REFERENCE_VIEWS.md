# Reference-count and borrowed-view conversion

This batch changes SkRefCnt.h, SkWeakRefCnt.h and SkSpan.h. Original headers remain
in the reference include tree. Three corner-array call sites in SkMatrix.h,
SkMatrix.cpp and SkDevice.cpp now bind a named array before making a borrowed view;
those larger files are not otherwise converted by this batch.

Reference-count arithmetic and atomic memory orders are preserved. Copy, move,
reset and weak-lock steps have explicit types and state transitions. Reset still
publishes the new pointer before releasing the old one, so release can reenter
the owner. Views reject temporary owning containers; temporary borrowed views
remain accepted. Empty views avoid arithmetic on a null pointer, and slice
lengths/offsets are checked before pointer computation.

## Verification

M4 macOS, non-Apple LLVM 23.1.2, C++20:

- Five standalone tests pass on both reference and candidate.
- The new test covers copy/move/self-assignment, transfer/release, reentrant reset,
  concurrent shared references, weak locking/expiry, valid slices of arrays with
  lengths 0 through 33, reverse traversal and const-view conversion.
- Compile-time checks reject rvalue vectors while accepting lvalue owners and
  borrowed std::span values. Initializer-list views remain statement-lifetime
  borrows; this conversion does not turn them into owning containers.
- All 24 raster outputs match the original reference byte for byte.
- Release instrumented suite: **PASS**.
- Debug instrumented suite with assertions enabled: **PASS**.
- The relocated installed consumer builds and runs.
- Seven selected GUI.Forms tests pass with its LLVM 22.1.8 toolchain.

ASan/UBSan checks exclude RTTI-dependent vptr instrumentation at the existing
no-RTTI Skia boundary. Threaded tests exercise ownership and do not constitute
a thread-sanitizer campaign or proof of every concurrent execution.

The spelling checker reports four compatibility declarations: operator-> and
three class-template deduction guides. They are recorded in the conversion
ledger and not suppressed. Remaining callers still need conversion before the
legacy access operator can be removed. The three headers plus the new test have
zero lifetime-checker findings. The new test has zero spelling findings.

## Measurements

Nine alternating trials, the same 24-case workload used in the first record:

| Draw time | Original reference | Candidate |
|---|---:|---:|
| Median | 9.970 ms | 10.193 ms |
| Range | 9.820–10.459 ms | 9.632–10.810 ms |

The candidate median is 2.24% higher with overlapping trial ranges. This is not
an established speed improvement, nor does comparison to a different session's
absolute time isolate this batch. The current archive is 4,884,728 bytes versus
5,200,272 for the reference. Raw arrays and hashes are in
[reference-views-measurements.json](reference-views-measurements.json).

## Negative results and platform follow-up

The initial constexpr slice assertion was rejected by the original implementation:
its checked slice helper is not constexpr. The compile-time test now checks the
supported pointer/size interface; the full slice grid is tested at runtime on
both implementations. The candidate then identified three temporary corner-array
arguments, fixed by naming their owners before constructing a view.

The earlier Windows job now builds and passes its four-test release suite on both
reference and candidate, with matching render hashes. It stopped before the
byte-comparison/install step because the MSYS2 runner lacked cmp. CI now uses
`cmake -E compare_files`, avoiding a new tool dependency. Corrected full native
CI remains a separate gate; these macOS results do not substitute for it.
