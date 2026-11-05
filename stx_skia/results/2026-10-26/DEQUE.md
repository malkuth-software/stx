# Block deque conversion — isolated verified branch

This experiment is isolated in /Users/ultimussecundai/stx_skia-deque on branch
codex/deque-conversion, based on verified commit d96ecc2. The pending TArray trial
in /Users/ultimussecundai/stx_skia and its review bundle were not modified.

SkDeque is retained by canvas save/restore records and the clip stack. The
conversion preserves its existing owner fields, linked blocks and delayed reuse
of empty end blocks. Original header/source are preserved under baseline/skia
from initial import 490c9bd4a97884d268a5b3f9924748ebf31954bb. The reference build
selects those originals.

The trial names slot ownership and invalidation, checks allocation-size arithmetic
at construction, makes block-boundary checks before pointer movement, uses the
existing initial-buffer pointer to distinguish borrowed storage from owned heap
blocks at release, and rounds borrowed storage to whole slots. Iterators reject
borrowing temporaries and have an empty fallback for invalid direction values.
Final-element removal explicitly clears both endpoint pointers, including when
empty blocks remain at both ends. This is a scoped conversion of block ownership,
slot arithmetic and iteration, not a repository-wide review.

## Evidence obtained

Native M4 macOS arm64, non-Apple LLVM 23.1.2:

- Both 44-test Release profiles passed.
- 36,368 ordinary deque operations agree with a std::deque value oracle. Candidate
  and reference allocation/count traces match: 14765068904130690563.
- 98,592 additional candidate-only mixed operations passed, using fixed schedules
  across heap and borrowed storage, element widths 1/3/8/64 and block sizes 1/7/32.
- Both release layouts report a 56-byte deque and 24-byte iterator.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Changed header, implementation and fixture pass house-style/lifetime spelling
  checks. These checks do not prove semantic correctness.

The original Release instrumented deterministic suite returned STOP. Daybreak
review found that the new candidate-only test cast an integer outside the
`IterStart` enum and passed that invalid value to `reset`; UBSan rejected the
test input. The test now checks a valid unbound iterator instead. No production
deque code changed for that STOP.

After correction, the M4 candidate and reference Release suites both passed 44
tests. Candidate Release and Debug instrumented suites report PASS. A relocated
installed consumer builds and runs, the CPU build-scope check passes, and nine
rendering/mipmap artifacts plus the serialized artifact match byte-for-byte.
The corrected test and source pass the house-style and lifetime spelling checks.
No performance measurement or full GUI.Forms integration is claimed for this
branch.

The conversion was subsequently cherry-picked onto `codex/cpu-library` beside
the TArray and bitmap fixes. The combined candidate/reference Release suites
pass 45 tests each; Release instrumentation reports PASS, ten rendering and
serialization artifacts match, the CPU build-scope check passes, a relocated
installed consumer runs, and seven selected GUI.Forms renderer tests pass.
The branch's independent Debug instrumented PASS remains separate evidence;
the combined candidate has not yet run that Debug profile.

The failed source snapshot, fixture, runner, hashes, tracked patch and ordinary
gate log remain in `build/deque-stop/`. The matching M4 directory contains the
preserved STOP record. Do not overwrite either review bundle.
