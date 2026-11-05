# Convert the retained binary reader

SkReadBuffer.h/.cpp now follow the house style. The three borrowed cursor pointers,
version, procedure record and error bit remain; measured M4 Release size is still
64 bytes. There is no additional ownership registry, token, lock or reference count.
The original header and implementation remain byte-for-byte equal to the initial
Skia import in baseline/skia.

## Contracts and representation

Input storage stays owned by the caller, immutable and alive through the read.
A replacement extent is checked before cursor publication. Null storage is admitted
only with zero size; extents must be four-byte aligned and fit ptrdiff_t so cursor
differences are representable. Empty queries and zero-byte skips avoid null-pointer
arithmetic. Failure remains sticky, advances to the end, and cannot be cleared by
setMemory. A failed nonempty bulk read leaves its output unchanged.

Word/scalar/count reads copy bytes into typed objects instead of dereferencing
reinterpreted typed pointers into arbitrary serialized storage. M44 reads use a
local 16-float array for the same reason. String terminator arithmetic is checked.
Typed skip views explicitly require actual suitably aligned T objects. Procedure
records are initialized; their addresses/context remain borrowed. The getter for
the reader's procedure record rejects temporary readers. Image callbacks, optional
mipmaps, placeholder behavior and the disabled effect policy are retained.

Owned byte-array reading now validates and skips the entire padded record before
allocating the result. SkData::MakeWithCopy places its header and bytes in one
allocation, replacing the separate temporary malloc buffer and SkData wrapper.
It still copies the same payload once and returns independently owned bytes.
There is no retained scratch capacity or caching. These boundary and allocation
changes are deliberate, not a claim of a purely mechanical rewrite.

## Controlled measurements

M4 macOS arm64, non-Apple Clang 23.1.2, Release. The comparison substitutes only
SkReadBuffer.h/.cpp from 6c91ea4 and links the current dependency library, including
the current SkData implementation. The comparison builder verifies the prior
source digests. Correctness output agrees. Seven alternating paired trials follow
one warmup per executable; raw ranges are in READ_BUFFER_MEASUREMENTS.json.

| Workload | Prior median | Converted median | Converted/prior |
|---|---:|---:|---:|
| Owned 16-byte record | 33.10 ns | 16.86 ns | 0.510 |
| Owned 256-byte record | 34.27 ns | 24.76 ns | 0.723 |
| Owned 4,096-byte record | 77.28 ns | 62.07 ns | 0.803 |
| Read one word | 0.98 ns | 1.03 ns | 1.055 |

Each owned-record trial includes reader construction, allocation, copy, two endpoint
reads and destruction, repeated 131,072 times. Word trials sum 4,096 words through
8,192 reader passes. The word median regresses about 5.5%, with overlapping ranges
(0.96–1.04 ns prior; 0.96–1.04 ns converted). This is not an overall rendering gain.

Four earlier trials are retained as READ_BUFFER_MEASUREMENTS-PRIOR-1 through -4.
The first removed the alignment guard, the second used the established four-byte
cursor invariant to reduce the bounds test to an end comparison, and both roughly
doubled measured word cost. A guard-plus-end variant reduced that regression to
27%. Restoring the original alignment/capacity guard recovered near parity. Native
assembly showed that the shorter source changed load pairing/control flow; that
observation is not a proven microarchitectural attribution. All three primitive
read paths retain the original guard shape. The final word trial was lengthened
from 512 to 8,192 passes; earlier timings retain their own recorded scope.

## Verification

- Both Release profiles pass all 33 tests, including the new read_buffer fixture.
- The new fixture checks integer/float bit patterns, 258 byte-array lengths,
  independent output ownership, strings/padding, numeric arrays, points, rectangles,
  matrices, paths, regions, sampling modes, raw/data image callbacks, mipmaps and
  no-decoder placeholders. Fixed boundary cases check arithmetic overflow, invalid
  strings, empty state, replacement, sticky failure and invalid input/output extents.
- Nine render/mipmap artifacts and the 24-case, 3,344-byte effect-stream artifact
  remain identical to the original reference.
- Final Release and Debug instrumented deterministic suites: PASS. Earlier logs
  were preserved; detailed diagnostic files were not read. No new randomized
  memory review was run.
- Fresh relocated installed consumer and seven selected GUI.Forms tests: PASS.
- Changed C++ passes house-style and lifetime checks; source/archive scope passes.
- The preceding 6c91ea4 commit passes Linux x64, Linux arm64 and Windows CI. This
  conversion still requires its own current-head platform gates after publication.

Reproduce normal gates with tools/build_native.sh and tools/verify_gui.sh. For the
controlled comparison, export the two reader files from 6c91ea4 into
build/read-buffer-prior-source/src/core/, preserving those filenames. Run
`python3 -B tools/build_read_buffer_comparison.py` then
`python3 -B tools/measure_read_buffer.py` sequentially on the measurement host.
The comparison uses the candidate compile commands and Ninja link command. For the
M4 mirror, copy the exported prior sources into its build directory separately;
ordinary m4build synchronization excludes build/.

The writer, private reconstruction implementations, retained geometry and raster
kernels are separate remaining conversion work. This record covers the reader.
