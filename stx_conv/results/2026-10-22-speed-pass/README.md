# Speed pass record, 2026-10-22 (provisional)

Windows 11 VM, AMD EPYC 9354 (4 cores, 8 threads), LLVM 22.1.8 (MSYS2
CLANG64), `-O3 -DNDEBUG -ffp-contract=off`, baseline x86-64. Other builds held
the CPU at or near 100 % load during every run (the load is recorded in each
file), so all figures are provisional. See `../../docs/OPTIMIZATION.md`,
"Speed pass".

- `image_ab.txt`: before (A) and after (B) the pass, alternating; warm and
  cold resize times, thread cycles (QueryThreadCycleTime) and output digests.
- `face_timing.tsv`: `tools/resampler_face_study --timing`, before and after,
  alternating; the face digests are unchanged.
- `probes.txt`: the same code built for x86-64-v3 and x86-64-v4, and a
  reciprocal-multiplication variant (changes bits); none adopted.
- `bench.cpp.txt`, `ab.sh.txt`, `ab_summary.py.txt`: the bench (Windows only:
  it reads thread cycles), the alternation script and its summary, kept as
  text so they are not built.
