# Validation record

Date: 2026-10-07. Implementation is independent, under `stx_vorbis/` only; the
provider's existing audio decoder and sibling threading work are unchanged.

## Local results

Environment: Apple M4 Mac mini, macOS 26.5, Homebrew LLVM 22.1.8. Core standalone
builds target macOS 14.0. Reference libraries installed by Homebrew require 15.0;
their test-only executable produces linker minimum-version warnings. Core and
adapter source compile without warnings under `-Wall -Wextra -Wpedantic
-Wconversion -Wshadow -ffp-contract=off`.

- Release and AddressSanitizer/UndefinedBehaviorSanitizer suites: scalar transform
  versus a direct cosine sum, scalar/NEON agreement, streaming chunk sizes 1/7/255/
  1023/4096/whole-file, partial consumption, event/PCM backpressure, chains, format
  changes, sample seeking, pure-C adapter, bounded supplied arenas with repeated
  seeks, 70,000-byte continued Ogg packet, CRC/recovery, Huffman entry order, and
  failure at each allocator ordinal through a successful decode.
- x86_64 executable under Rosetta: scalar/SSE2/AVX2 transform comparisons passed.
  This is x86 execution evidence, not a substitute for native Windows/Linux CI.
- Generated libvorbis comparison: six ordinary cases (8–96 kHz, 1/2/6/8 channels,
  17–96,017 frames), all six floor/residue combinations, negative-start trim,
  positive-start offset, and a 14-link format-changing chain. Exact sample counts.
  Ordinary-audio maximum float error: `2.384185791015625e-7`. Synthetic high-amplitude
  stress maximum: `1.9073486328125e-6`. Chain: 741,000 interleaved float values.
  The reproducible acceptance bound is `2e-6 + 2e-6 * abs(reference)` per sample.
- First six-stage ASan/UBSan libFuzzer campaign, approximately 46 seconds per target:

| Target | Executions | Sanitizer finding |
|---|---:|---|
| Bit reader | 7,986,429 | None observed |
| Ogg demux | 35,830 | None observed |
| Huffman | 2,171,677 | None observed |
| Setup parser | 188,531 | None observed |
| Packet decoder | 96,207 | None observed |
| Complete streaming decoder | 2,026 | None observed |

These are finite initial runs, not a vulnerability-free assertion. Fuzzing is
repeated in Linux CI and should continue with a larger corpus and longer budgets.
The checked-in fixtures are generated originals; reference source/audio downloads
are outside Git.

## Independent references and retained differences

- libvorbis 1.3.7 / libogg 1.3.6 installed libraries; source inspected at Xiph
  `1b75110b5a2754ba1931d82dd83cb822b266a21d`.
- Tremor source `820fb3237ea81af44c9cc468c8b4e20128e3e5ad`, compiled independently.
  Ordinary generated audio differs by at most one int16 unit. The synthetic floor-0
  stress fixtures differ by up to 1,546 int16 units; these fixtures deliberately
  exercise large spectral values/resonant curves. This disagreement is retained
  and has not been resolved into a complete explanation of Tremor's fixed-point
  approximations. stx agrees with libvorbis's float PCM on these fixtures.
- stb_vorbis 1.22 from miniaudio commit
  `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`. Ordinary mono/stereo/eight-channel cases
  agree within `2.4e-7`. The six-channel generated case and single-entry-book stress
  fixtures show large discrepancies (up to about 27.1 float units in stress data).
  These are observed baseline differences, not a claim to have diagnosed every
  stb defect. stb rejects floor 0, so it cannot serve as that oracle.
- Historical floor-0 sample `01_Duran_Duran_Planet_Earth.ogg` from the
  [FFmpeg sample collection](https://samples.ffmpeg.org/A-codecs/vorbis/floor_type_0/):
  1,758,656 frames decoded before a missing ending is reported as `truncated`.
  Compared with FFmpeg's float output: max `2.326071262359619e-5`, RMS
  `1.8102090757330143e-6`. The sample is not redistributed.
- A chain beginning with the 17-frame 8 kHz fixture made libvorbisfile 1.3.7's
  seekable-file path omit that first link's PCM; decoding it individually returns
  all 17 frames. The standard differential chain starts with the longer mono link;
  stx's independent chunk/chain test still checks concatenated per-link output,
  including tiny initial links. This reference discrepancy remains documented.
- A deliberately backdated EOS granule that attempts to remove samples preceding
  the final packet is rejected as `invalid_packet`. libvorbis returned already
  emitted PCM on that malformed test. Vorbis I appendix A.2 defines final-packet
  trimming, not arbitrary retroactive removal of earlier emitted packets.

The reference build emits warnings in external Tremor/stb source. Those source
files have not been modified or represented as house-style-compliant.

## Size and initial timing

Observed local Release archives before final documentation/CI edits: core ~136 KiB,
C adapter ~26 KiB; decoder command-line executable ~122 KiB. Static archive size is
not a consumer's incremental linked size; dead stripping and runtime linkage vary.

For the 96,017-frame stereo 48 kHz generated fixture, 15 process-level decode runs
writing PCM to `/dev/null`: the initial 4N FFT implementation's median was 10.13 ms;
the N-point modulation implementation's median was 5.15 ms. This includes process
startup, setup, file I/O and conversion, and was not a controlled playback or
real-time audio-callback benchmark. It does not establish a speed claim against
libvorbis or stb. The direct transform and differential tests were rerun after the
change.

## Cross-platform status

The focused workflow `.github/workflows/stx-vorbis.yml` runs LLVM 22 builds and the
same tests/differential corpus on macos-15 arm64, ubuntu-24.04 x64,
ubuntu-24.04-arm, and windows-2022 CLANG64. Linux jobs also run sanitizer tests and
all six fuzz entry points. Actual CI results are recorded in the pull request;
workflow presence alone is not a passing-platform claim.

## Assurance still required

No machine-checked formal proof or independent security audit has been completed.
The finite corpus is not exhaustive for all valid codebook/mapping combinations,
all damaged broadcasts, all allocator implementations, or all C-adapter call
sequences. This implementation remains separate from the default audio path so
integration can be reviewed on its own evidence rather than silently changing
application decoding during the sibling's threading work.
