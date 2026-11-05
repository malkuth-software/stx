# TArray construction and live-count publication

This is the construction phase of the TArray/STArray conversion, not a claim
that the complete array implementation is converted. The original full header
is preserved byte-for-byte under baseline/skia from import commit
`490c9bd4a97884d268a5b3f9924748ebf31954bb`.

## Changed operations

Nontrivial copy construction, copy assignment and reset publish their live count
after construction succeeds. Default single/bulk append does the same.
Nontrivial copy/default construction uses the standard uninitialized algorithms,
which destroy their partial destination range if construction throws. Trivially
copyable elements keep the direct byte-copy branch, including types with a deleted
copy constructor. That nonthrowing branch preserves the original count-store
before the byte copy. Default scalar initialization remains uninitialized.

Delegating constructors establish a valid empty owner before allocation or element
construction. A failed constructor then releases its storage through that owner's
existing destructor. Inline storage likewise starts with zero live elements; its
base remains alive while the array owner cleans up. There is no added owner,
registry, reference count, persistent exception state or live-count update per
constructed element. Production remains built without exception handling; the
exception-enabled test consumer proves these construction-failure paths.

Reset/copy-assignment construction failure leaves an empty owner retaining its
current capacity. Default-append construction failure preserves the prior live
count and elements, though capacity may already have grown. This statement covers
failure of the newly appended elements' default construction, not throwing
relocation of previous elements: that separate path remains to be converted.

Span and initializer-list lengths are checked before conversion to int. Reset
from a live nontrivial subrange of the same array snapshots that subrange before
destroying its source. Only this nontrivial alias case allocates the additional
temporary copy. Trivial reset uses memmove, so an overlapping subrange requires
neither a snapshot nor an alias test. Disjoint reset keeps its existing reuse/growth
policy. If a nontrivial snapshot fails, the original is unchanged; if reconstruction
afterward fails, reset leaves it empty.

## Fixtures

- 421 ordinary schedules cover scalar and nontrivial copy construction, span and
  initializer-list construction, copy assignment, reset, zero append and bulk
  default append across inline and heap boundaries.
- A separate trivially-copyable/deleted-copy-constructor fixture preserves the
  existing byte-copy capability.
- 264 candidate-only failures interrupt construction at each possible prefix,
  testing constructor cleanup, count/span reset, copy assignment and default
  single/bulk append. The reference does not execute these previously invalid
  failure paths.
- 2,112 candidate-only alias resets exercise each suffix of 1–32 live trivial and
  nontrivial elements, in both heap and inline arrays. Trivial cases also verify
  that their original storage is retained.
- Heap and four-element inline array layouts remain 16 and 32 bytes respectively
  in both native release builds. No instance field changed.

## Remaining array work

Move construction/assignment, relocation (including the native bit-relocation
contract), reference/repeated-value append, move append, removal, complete borrow
qualification and remaining arithmetic/style cleanup are still outstanding.
In particular, these tests do not establish throwing-move safety or sanitizer
unused-capacity poisoning after an exception. An exception can leave the existing
allocation unpoisoned until a subsequent successful size change; object counts
and cleanup remain correct in the covered construction paths.

## Original stopped trial and follow-up

The initial construction implementation and the revision retaining direct trivial
copy both passed 44-test candidate/reference Release suites, byte comparisons,
and Release/Debug instrumented deterministic suites. The first timing comparison
showed candidate/reference ratios of 1.205 and 1.584 for 16-element copy assignment
and disjoint span reset; 1.163 and 1.121 at 256 elements; 1.038 and 1.028 at 4096.
Those negative measurements are retained in
`build/tarray-construction-prior-measurements.json`.

A subsequent fast-path trial kept trivial count publication before copying and
used memmove for trivial reset. Both 44-test Release suites and rendering artifact
comparisons passed, including 2,112 alias resets. Its Release instrumented suite
returned STOP. The gate stopped there; Debug, installation, GUI.Forms integration
and final fast-path timing are not established for this trial.

The Daybreak follow-up traced the stopped trial to trivial reset from a live
subrange: it cleared and poisoned the source before copying it. The corrected
path moves the bytes to the front while the source remains live, then changes
the live count. This preserves the no-allocation trivial fast path. The stopped
trial remains preserved under `build/tarray-construction-stop/`; it is historical
failure evidence, not the current gate result.

The follow-up also changed public count checks from debug-only assertions to
release assertions, checked `size_t` before `resize` narrows it to `int`, and
removed null-pointer arithmetic from empty `end()` and zero-length raw append.
The construction regression includes a zero-length append on an empty owner.

On the current source, native M4 candidate and reference Release suites each
pass 44 tests. Candidate Release instrumentation reports PASS, as does the
Debug instrumented 44-test suite. Nine rendering/mipmap artifacts and the
serialized artifact match byte-for-byte, and the CPU build-scope check passes.
The last changes to empty `end()` have no timing measurement, so the earlier
negative timings above remain the only recorded construction timing evidence.
An installed consumer built and ran from a relocated fresh candidate prefix.
The remaining array work listed above still blocks a full TArray bill of health.
