# stx_vorbis

Independent C++20 Ogg/Vorbis I decoder. It does not call or link stb_vorbis,
libvorbis, Tremor, miniaudio, GUI.Forms, or platform audio APIs. Those decoders
are test oracles only. The existing GUI.Forms audio implementation is unchanged.

This is a new implementation undergoing validation, not a security certification.
See [validation](docs/VALIDATION.md) for measured coverage, reference disagreements,
and remaining assurance work. See [optimization measurements](docs/OPTIMIZATION.md)
and the [Shadow x86 handoff](docs/SHADOW_X86_HANDOFF.md) for comparative timing,
hot assembly, reproducible inputs and the remaining performance gap. See [invariants](docs/INVARIANTS.md) for the parsing,
allocation, numerical, and ownership review.

## Build and import

```sh
cmake -S stx_vorbis -B build/stx -DCMAKE_BUILD_TYPE=Release
cmake --build build/stx --parallel
ctest --test-dir build/stx --output-on-failure
```

Use `add_subdirectory(path/to/stx_vorbis)` and link `stx_vorbis::stx_vorbis`.
The library requires C++20, IEEE binary32/binary64, and exception support at the
construction/parser failure boundary. Ordinary streaming errors are status values.
A C caller links `stx_vorbis::stx_vorbis_stb` instead and includes
`<stx_vorbis/stb_compatible.h>`. Do not link that adapter together with stb_vorbis:
the adapter deliberately exports the same C symbol names.

`cmake --install build/stx --prefix <prefix>` installs both static libraries,
headers, and a `find_package(stx_vorbis CONFIG)` package. SIMD uses runtime CPU
selection; there is no requirement to compile the consumer with `-mavx2`.
No compiler caches, GUI.Forms pins, backend pins, or audio/threading targets are
changed by this directory. The standalone workflow tests LLVM 22 on the provider's
four runner images. macOS defaults to deployment target 14.0; Windows code names
Windows 10 explicitly. Test-only Homebrew reference libraries can require a newer
macOS version; they are never linked into the decoder.

## Capabilities

- Vorbis I identification/comments/setup, codebook lookup types 0/1/2, floors 0/1,
  residues 0/1/2, mapping 0, mode selection, channel coupling, both block sizes,
  all standard block sizes 64–8192 and up to 255 channels (subject to limits).
- CRC-checked Ogg framing with continued packets, sequence tracking, multiplexed
  serials, and bounded resynchronization. The public `OggDemuxer` is codec-independent.
  Empty EOS pages produce an explicit demuxer end event. Packets wholly on a page borrow the input page; continued packets use bounded
  retained assembly storage.
- Incremental push, borrowed-memory pull, owned/borrowed file pull, and a public
  `Source` interface. All use one packet/synthesis core. A caller never resubmits
  bytes counted in `FeedResult.accepted`.
- Borrowed planar float32 PCM, caller-owned planar/interleaved float32/int16
  conversion, explicit consumption, and one output block of backpressure.
- Sequential chained logical streams, including sample-rate/channel changes,
  metadata, begin/end events, and diagnostics. Vorbis channel order is preserved.
  Multiple concurrent Vorbis programs are explicitly `unsupported` in one Decoder;
  use separate codec instances over the reusable multiplexing demuxer.
- Exact stream-relative PCM seeking, including a named chain index. It decodes
  from BOS with bounded memory: seeking is linear in the target position, not an
  indexed/random-access performance claim.
- Negative start trimming, positive broadcast granule origins, final-packet
  trimming, gap diagnostics, and explicit `position_known` after recovery.
- Immutable validated setup, entry-order Huffman trees plus 10-bit prefix tables,
  prepared VQ vectors, MDCT plans, windows, floor-1 neighbors and floor-0 Bark maps.
- Portable scalar synthesis and NEON/SSE2/AVX2 double-precision butterfly kernels.
  Real/imaginary arrays and stage twiddles are contiguous. There are no matrix
  transposes, FMA intrinsics, blanket fast-math, or allocations in synthesis loops.

No resampler, playback device, thread pool, downmixer, encoder, or plugin ABI is
introduced. The C adapter retains stb's legacy integer channel coercion; native
C++ callers receive the encoded channel layout and choose their own conversion.

## Streaming protocol and lifetimes

`Decoder::advance()` returns one of:

| Status | Caller action |
|---|---|
| `need_input` | Offer another arbitrary chunk with `push`; retain only its unaccepted suffix. |
| `event` | Inspect `event()` and acknowledge it. `stream_begin` follows complete setup validation. |
| `pcm` | Inspect `output()`, consume some/all frames, then advance. |
| `end` | Clean end of all logical streams. |
| Error | Inspect `diagnostic()`. Fatal errors stay sticky until reset. |

Pass `final=true` on the final chunk (or an empty final push). Final becomes
accepted only when the entire offered span is accepted. `need_output` from push
means the bounded input buffer is full: drain the decoder before offering the
unaccepted suffix. Repeated advance calls while PCM/events are outstanding return
the same state. No hidden output queue grows behind them.

A `PcmView` has channel-major storage, `stride` floats between channels, and
`frames` live floats per channel. Its samples survive partial consumption, but the
next advance that decodes a packet may overwrite them. Reset/destruction also
invalidates them. Copy to caller-owned storage before retaining PCM asynchronously.
`copy_*` does not consume PCM; destination storage must not overlap it. int16
conversion multiplies by 32768, rounds nearest with ties away from zero, and
saturates. NaN passed to the conversion utility becomes zero; the decoder itself
rejects nonfinite synthesis output.

Metadata is retained as length-bearing raw strings, including duplicate comments.
`tag(key, occurrence)` performs ASCII-insensitive key matching; comments are not
HTML, paths, or trusted UI markup. Metadata views survive PCM consumption. After
acknowledging `stream_end`, the next advance can invalidate them while reading the
next chain; reset/destruction also invalidates them. Copy metadata you retain.

Objects belong to one calling execution context. Separate decoders have separate
mutable state. A supplied `std::pmr::memory_resource` is borrowed and must outlive
the decoder; its own synchronization policy remains the caller's responsibility.

## Bounds, allocation, and diagnostics

Default limits are 128 MiB total decoder-owned storage, 256 KiB buffered input,
16 MiB assembled packet, 1 MiB metadata text, 16,384 comments, 16 concurrent Ogg
serials, 4,096 chains, 1,048,576 entries per codebook, 8,388,608 expanded values per
book, 1 MiB resynchronization distance, and 32 Mi abstract packet work units.
Work accounting bounds entropy/VQ, floor-0, coupling, transform and basic sample
work; it is not a wall-clock deadline. Geometry bounds the other fixed loops.
Input storage must hold a maximum Ogg page (65,307 bytes). A single output block
is at most 4,096 frames/channel. Lower limits can intentionally reject valid files.

Construction can throw `std::bad_alloc`, `std::length_error`, or
`std::invalid_argument` for invalid Ogg buffer/stream limits. After construction,
push/advance/consume/seek return explicit status values and contain parser/allocation
failures. A custom allocator must follow the standard memory_resource contract.
Setup is published only after validation and workspace preparation succeed.

`StreamInfo` reports rate, channel/block sizes, codebook/floor/residue/mapping/mode
counts, chain/serial identity, granule origin, emitted frames, next sample coordinate, known total, timeline
status, and exact requested allocation bytes. `setup_bytes` includes the setup
object and retained setup allocations; `workspace_bytes` counts retained workspace
capacities. `current_bytes` and `peak_bytes` include decoder/Ogg state, input,
metadata, setup, and scratch. They exclude allocator bookkeeping, caller-owned
Source/PullDecoder objects (including the 32 KiB pull buffer), C adapter objects and
its arena/pool overhead, and C library FILE internals. Peak is lifetime peak; it
is not reset at chain boundaries. Totals stay unknown until an EOS page is read.

Strict mode rejects framing corruption, sequence holes, invalid setup/packets,
resource exhaustion, and truncation. Resynchronizing mode emits diagnostics,
discards unsafe partial packets/overlap, and resumes at checked framing. It never
inserts invented silence to disguise a hole. `timeline_discontinuous` stays true;
`position_known` becomes true again at a trustworthy granule. Vorbis's legal
packet peeling can exhaust floor/residue bits; that is handled as specified, not
misreported as an out-of-bounds read. Recovery cannot reconstruct lost audio.

## C adapter compatibility

The adapter covers the public memory/file/file-section/push, metadata, frame/sample,
float/short, seek, length, offset, close and error entry points. C exceptions never
cross the boundary. Provided stb allocation buffers are used as bounded
`std::pmr` arenas/pools with no heap fallback for decoder storage. Too-small arenas
fail explicitly. Whole-file convenience functions return `malloc`-owned int16
storage for `free()` and impose a 128 MiB output limit.

The legacy adapter exposes the first logical stream, matching its single-format
contract. Use the native API for chain events and format changes. Float output
keeps/truncates/zero-fills requested channels; integer coercion follows stb's
historical mono/stereo rules. `seek_frame` is strengthened to an exact seek.
Memory reports describe the new implementation, not stb's allocation layout.
Push calls may accept bytes before producing PCM; accepted bytes are retained.
Drain buffered frames with zero-byte calls even after all source bytes were
accepted. `flush_pushdata` preserves setup, drops transport/overlap, and marks the
position unknown until a later granule. Build-time stb macros are not reproduced;
choose the separate C adapter target when compatibility symbols are wanted.

## Verification

```sh
cmake -S stx_vorbis -B build/stx-asan -DCMAKE_CXX_COMPILER=clang++ \
  -DSTX_VORBIS_SANITIZERS=ON -DSTX_VORBIS_FUZZ=ON
cmake --build build/stx-asan --parallel
ctest --test-dir build/stx-asan --output-on-failure
python3 stx_vorbis/tools/seed_fuzz.py build/seeds stx_vorbis/tests/corpus/*.ogg
build/stx-asan/stx_fuzz_packet build/seeds/packet -max_total_time=60
```

Six fuzz executables cover bits, Ogg, Huffman, setup, packet, and full streaming
paths. The tiny checked-in corpus is generated from original test signals by
`tools/make_synthetic_corpus.py`. With libvorbis development packages installed,
configure `-DSTX_VORBIS_REFERENCE=ON` and run `tools/differential.py --build <build>
--work <scratch>` for the broader generated and chained corpus. External reference
code and historical audio samples remain outside this repository.

## Specification and provenance

The algorithm contracts are the [Xiph Vorbis I specification](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html)
and [Ogg framing specification](https://xiph.org/ogg/doc/framing.html).
Reference implementations were read and tested as documented in the validation
record; no external decoder source is linked or vendored here. Public C++ API
version 0.1 is a source-level integration surface, not a frozen cross-project ABI.
