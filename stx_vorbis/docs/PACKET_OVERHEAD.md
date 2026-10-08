# Non-transform packet overhead: Shadow, 2026-10-08 UTC

**GIVEN:** Look for avoidable work outside the FFT and reduce nested branching
where a clearer representation preserves behavior. Provider baseline:
`3c6f89c5b25e60d7277758f44524bdbcdd90eb67`. Consumer pins are unchanged.

## Selected change

**MEASURED:** Express inverse coupling with two sign predicates and three
selections. The magnitude/angle sign relation selects the original subtraction
or addition. The angle sign selects which output receives the adjusted value.
The two channel spans are modified in place; no temporary buffer is introduced.

The previous nested loop was scalar under Clang's cost model. A preliminary
vectorization-hint experiment improved complete decode time. The final flat
representation auto-vectorizes at width two without the hint. The inspected x64
vector body uses comparisons, masks, one packed addition and contiguous loads
and stores. It has no data-dependent branch inside the vector body. There are
still loop, extent/alias and scalar-tail branches; a ternary alone does not
promise branchless machine code. Compiler lowering may select a negated operand;
the source keeps the original addition/subtraction rather than hand-reassociating
the arithmetic. No fast-math or new instruction-set requirement is enabled.

`inverse_couple` is a private, named kernel with equal-size, disjoint borrowed
spans. Setup parsing already validates distinct, in-range coupling channels.
The spans cover only active bins, never padding. Reversed coupling-step order,
resource charging, packet failure behavior and floor validation are unchanged.
`synthesis.cpp` and FFT dispatch are unchanged.

## Timings and controls

**MEASURED:** Native Windows 11 Shadow guest, AMD EPYC 9354 presented as four
cores/eight logical processors, Balanced power plan, CLANG64 LLVM 22.1.8.
Release `-O3 -DNDEBUG -gline-tables-only -ffp-contract=off`; no LTO or global AVX2.
See the archived environment and exact executable/input hashes in each summary.
The checked-in stereo and six-channel inputs each contain 480,017 frames at
48 kHz. Each row aggregates three rounds of 21 measured iterations, after three
warmups per invocation. Implementation order rotates within each run. Timing is
memory input, constructor through EOF, excluding destruction; correctness is
checked separately over complete output. No owned profiling or builds overlapped
these timing runs. This shared guest still exhibits scheduler noise.

| Input / decoder | Baseline median / p95 ms | Flat coupling median / p95 ms | Repeat baseline median / p95 ms |
|---|---:|---:|---:|
| Stereo, stx AVX2 | 20.5068 / 21.6130 | 18.3980 / 19.3860 | 20.6324 / 21.3722 |
| Six-channel, stx AVX2 | 58.1006 / 59.3809 | 51.5218 / 55.1606 | 58.3493 / 60.3994 |
| Stereo, libvorbis control | 17.1516 / 18.3234 | 17.2879 / 18.0390 | 17.2313 / 17.8059 |
| Six-channel, libvorbis control | 43.0582 / 44.3917 | 43.1506 / 44.6709 | 43.1865 / 44.2009 |

The selected stx path takes **10.3% less time on stereo and 11.3% less on
six-channel** relative to the first baseline; the repeated baseline agrees.
This does not establish Mac/Linux speedups. Scalar and SSE2 dispatch results
are also archived; these select transform kernels, while coupling is shared.
The flat run has large six-channel scalar/Tremor tail spikes, retained in the
raw measurements rather than filtered out. Existing six-channel stb PCM errors
exclude that decoder from correct-output rankings despite matching counts.

**MEASURED:** Decoder-owned peak allocation is unchanged: 1,480,656 bytes stereo,
1,884,944 bytes six-channel. Workspace remains 131,072 / 327,680 bytes. This is
accounted decoder allocation, not process RSS. Benchmark PE `.text` grows by
576 bytes (86,950 to 87,526); the executable grows by 1,024 bytes (363,520 to
364,544), including its debug-line information. These are not package sizes.

## Audit and rejected experiments

**OBSERVED:** Contiguous Ogg packets already borrow page storage; only fragmented
packets require assembly. Workspaces are reused across ordinary packets.
Memory-source input still passes through a pull buffer and demux storage, and
overlap synthesis copies a window half. The diagnostic observations did not
identify these copies as the leading non-transform cost. Removing a copy can
change borrow lifetimes and is not justified by an assumed benefit.

**MEASURED, diagnostic only:** An owned-child suspend/context/resume sampler
identified coupling, finite checks, residue scatter and Huffman work as useful
non-transform sites. These are perturbed wall-clock instruction observations,
not ETW CPU percentages. The initial timing run overlapped this sampler and is
explicitly rejected; `before-clean` is the uncontended baseline.

| Experiment | Stereo / six-channel AVX2 median ms | Disposition |
|---|---:|---|
| Byte CRC lookup table | 21.0098 / 57.7460 | REJECTED: no convincing whole-decode gain |
| CRC plus rolling residue channel/bin cursor | 21.1471 / 57.6010 | REJECTED: insufficient demonstrated gain |
| Above plus coupling vectorization hint | 18.5753 / 52.7942 | Isolated the promising coupling change |
| Coupling hint alone | 18.5960 / 52.4064 | Superseded by the clearer flat representation |
| Coupling hint plus deferred finite reduction | 18.6064 / 51.7752 | REJECTED: small/inconsistent gain; keep early failure |

Trials, summaries and rejected source patches are retained in the evidence
directory. None of the CRC, residue-addressing or finite-reduction edits are in
the selected production change. Full local trial binaries and raw runs remain
under `.build/stx-overhead*`; the portable archive includes trial summaries and
the source needed to reconstruct the rejected variants.

**OBSERVED unresolved oracle edge:** New residue-2 fixtures with begin offset one
and three/five channels exposed a baseline disagreement with libvorbis. Baseline
and cursor-trial three-channel PCM have identical SHA-256
`03cd05f9e00f1f4106a3ddac43bdfaca7d15e862d77e00546615dd0744ab5939`.
An independent interleaved-spectrum check passes. Vorbis specification 8.5/8.6
defines offsets on the interleaved vector; libvorbis 1.3.7's
`vorbis_book_decodevv_add` starts `chptr=0` and `i=offset/ch`.
**HYPOTHESIS:** Its handling of an offset not divisible by channel count explains
the difference. No compatibility change or new differential exception is made.
The fixtures, independent check and failed oracle log are retained under
`rejected/` for separate correctness investigation; all pre-existing differential
cases remain enabled. Sources: [Vorbis specification](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html)
and [libvorbis codebook.c](https://github.com/xiph/vorbis/blob/v1.3.7/lib/codebook.c).

## Correctness and source review

**MEASURED:** Release 7/7 CTests; native Windows ASan/UBSan 5/5 CTests. The new
coupling test covers all four quadrants, both signed zeros, subnormals, extremes,
infinities and quiet NaNs, zero/short/odd lengths through 4,096 bins, and offset
spans with sentinels. Non-NaN outputs match bit representations; NaN classification
is checked, not payload or floating-point exception flags.

All 17 complete outputs (existing synthetic corpus, generated ordinary inputs,
chained input and both benchmark inputs) are byte-identical to baseline.
The existing libvorbis differential suite passes. Every benchmark output float
is finite and inside `2e-6 + 2e-6*abs(reference)`: maximum errors are
2.384185791015625e-7 stereo and 2.682209014892578e-7 six-channel. CI owns native
Mac/Linux validation and the Linux fuzz stages; local Windows results do not
claim those completed. Existing operation/memory limits are unchanged.

House-style semantic review covers the authored coupling kernel/call site in
`src/packet.cpp`, its contract in `src/packet.hpp`, `tests/coupling.cpp`,
`bench/tools/compare_exact_pcm.py`, and the CMake test registration. Review checks
explicit types, named behavior, initialization, active extents, disjoint borrows,
load-before-store ordering, selected arithmetic, short inputs, scratch reuse,
failure effects and allocation. No callbacks or retained borrows are introduced.
The spelling scan reports 29 C++ files and zero findings. This is not a claim of
whole-library semantic style compliance; the older nested floor/residue decoder
was inspected for performance but not comprehensively refactored or certified.

## Reproduction and evidence

[Raw measurements and validation](../bench/results/2026-10-08-packet/) include
`before-clean`, `flat`, `baseline-repeat`, test logs, exact PCM hashes, allocation
reports, compiler remarks, and the final kernel assembly excerpt. Preserve a
baseline executable/library from the named revision and use the same compiler
and harness. Follow `SHADOW_X86_HANDOFF.md` for toolchain setup; add
`-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -gline-tables-only"` to reproduce these
line-attributed builds. Run one benchmark at a time:

```text
python stx_vorbis/bench/run.py --build .build/stx-perf --input stx_vorbis/bench/corpus/stereo-10s.ogg stx_vorbis/bench/corpus/surround-10s.ogg --output .build/stx-repeat --modes scalar sse2 avx2 --iterations 21 --warmups 3 --rounds 3 --compiler clang++ --label flat-coupling
python stx_vorbis/bench/tools/compare_exact_pcm.py --baseline BASELINE_DECODER --candidate CANDIDATE_DECODER --input INPUTS --output .build/stx-exact
```

Reversal is replacement of the private kernel call by the original quadrant
loop. No format, public API, ABI, buffer shape or consumer dependency pin changes.
