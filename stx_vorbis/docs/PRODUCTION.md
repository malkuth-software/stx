# Production BFFT integration and PlaySuite adoption

## Selection and ownership

**GIVEN, 2026-10-08:** finish production wiring, with PlaySuite as the first
consumer. BFFT's numerical kernel was already accepted; this change integrates
the bounded adapter into the shipping codec and its installation contract.

**OBSERVED:** `Synthesis::automatic` selects the double-precision prepared BODFT
adapter. `scalar`, `neon`, `sse2`, and `avx2` retain the original complex FFT
implementation. An unavailable explicit choice returns `unsupported` during
setup; it never silently selects a different implementation. Existing enum
values and constructor signatures are unchanged. BFFT's internal architecture
selection is compile-time and does not reinterpret these explicit selectors.

Setup owns immutable BODFT plans and rotation coefficients. Workspace owns
scratch and conversion arrays, shared across the decoder's channels and packets.
All allocations use the existing bounded memory resource. Only the selected
transform's tables and scratch are provisioned. A named function pointer is
selected at setup; no selector is evaluated inside a sample loop. Reset and
chained streams provision replacement plans through the same resource gates.
Failed provision releases partial storage and reports the existing failure
status; scratch is not usable until setup completes.

The numerical mapping, double precision, normalization and operation order are
unchanged from the previously validated bounded adapter. No blanket fast-math,
new FMA contraction, global allocator, plan cache or decoder worker is added.
The allocation report includes the embedded provider's plan and workspace bytes.
It still excludes upstream allocator bookkeeping and caller-owned PCM.

## Dependency and installed contract

The unmodified BODFT subset is vendored at BFFT revision
`0f75ca79fbdc9176af729fc67afed59dca436ff1`, accepted through BFFT PR #90.
The provenance manifest records normalized source hashes, verified by CMake.
Only the caller-provisioned translation unit is compiled. First-party wrappers
prefix its C symbols, opaque types and private C++ namespaces, so applications
may also link BFFT independently.

The existing `stx_vorbis::stx_vorbis` archive contains the implementation.
`stx_vorbis::stx_vorbis_stb` remains the compatibility adapter. No extra BFFT
library, public include directory, CMake target, checkout or download is required
by installed or source consumers. Installed license files are:

- `share/licenses/stx_vorbis/LICENSE`
- `share/licenses/stx_vorbis/BFFT-LICENSE.txt`
- `share/licenses/stx_vorbis/BFFT-provenance.json`

The old source-projection harness is retired. Its exact version and historical
measurement commands remain at GUI.Forms commit
`b89c21134bf321d91e9953df8f193b27c97f5328`; recorded results were preserved.
Current native CI tests the shipping target on Windows x64, macOS arm64,
Linux x64 and Linux arm64. It compares full PCM with explicit scalar and
libvorbis, tests a relocated installation and independent BFFT coexistence,
and retains all six Linux sanitizer/fuzz stages.

## PlaySuite adoption

PlaySuite continues including `<gui_forms/audio/audio.hpp>` and linking only
`GUIForms::Audio`. There is no application-side decoder or FFT selector to add.
Build the provider with audio enabled and `GUI_FORMS_AUDIO_VORBIS_BACKEND=stx`.
Fresh builds already default to stx; specify it explicitly when reusing an older
CMake cache. Rebuild and reinstall the SDK before rebuilding the consumer.
The explicit `stb` fallback remains available.

Use the provider's LLVM 22.1.x toolchain contract for both the SDK and its C++
consumer. Do not mix the historical PlaySuite GCC/LLVM 20 archives with the new
SDK. The full PlaySuite source build also uses GUI.Forms text-mask/bar-audio
surfaces beyond the installed Audio component; this audio validation does not
claim that the entire application has migrated to an installed-only SDK.

Consumer adoption must explicitly review its toolkit revision, compiler/cache
selection and notices. `games/tools/package_common.py` currently assumes the
stb license; an stx package must carry the stx and BFFT notices above plus the
existing miniaudio notice. Preserve frozen toolkit snapshots and old SDKs.
This provider change does not update File Manager or PlaySuite dependency pins.

## Validation and review

Native environment: Shadow Windows x64, AMD EPYC 9354 guest, Clang 22.1.8,
CMake 4.4.4, Ninja 1.13.2; release builds use the existing strict numerical flags.

**MEASURED:** nine standalone release tests pass; seven codec tests pass under
ASan/UBSan. Tests exercise all supported transform block sizes, an independent
cosine oracle, full streaming output, available explicit SIMD modes, unavailable
mode rejection, exact allocation reporting, one-byte-short budgets, allocation
failure sweeps, reset and chains. Transform execution requests no allocations.
Fifteen generated/checked-in/chained files pass the libvorbis differential gates.
Two ten-second benchmark inputs additionally pass full PCM comparisons against
explicit scalar and libvorbis. The relocated installed-package streaming and
independent-BFFT coexistence tests both pass.

**MEASURED first consumer:** PlaySuite revision
`15d7b74119e7d1d2ddc95e270a4328b1aedf6d7c`, unmodified
`tests/audio_tests.cpp`, compiled with LLVM 22.1.8 against the newly installed
GUI.Forms SDK using only `find_package(GUIForms CONFIG REQUIRED COMPONENTS Audio)`
and `GUIForms::Audio`. With the existing `.build/runtime-040` Ogg assets, all
424 files decoded. All 27 music loops had the exact asserted frame count and
zero measured offline playback-seam error. This is an audio consumer test,
not a full PlaySuite installer or interactive device test. Its source tree and
frozen SDK/toolkit inputs were not changed.

Full PCM from all 17 generated, checked-in, chained and benchmark inputs was
byte-identical to the preserved pre-BFFT shipping decoder on this Windows host.
Raw [comparison and PlaySuite results](../bench/results/2026-10-08-production/)
are checked in. The native GUI.Forms build and `gui_forms_audio_tests` also pass.

After builds finished, a shipping-path timing check used three warmups and 21
complete decodes per case, sequentially, with input bytes already in memory:

| Ten-second input | Automatic BODFT | Explicit AVX2 old FFT | libvorbis |
|---|---:|---:|---:|
| Stereo | 14.1728 ms | 18.7245 ms | 17.0387 ms |
| Six channels | 37.6397 ms | 52.2079 ms | 43.4320 ms |

These are median total decoder times on this Windows host, including decoder
setup. This is a production-path sanity measurement, not a cross-platform speed
claim or a replacement for the earlier alternating-round benchmark evidence.
Raw observations are in the same results directory. Commands use
`stx_bench input.ogg 21 3 automatic`, `stx_bench input.ogg 21 3 avx2`, and
`stx_bench_libvorbis input.ogg 21 3` from `.build/stx-production/bench/`.

Reproduction build commands are:

```sh
cmake -S stx_vorbis -B .build/stx-production -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release -DSTX_VORBIS_REFERENCE=ON -DSTX_VORBIS_BENCHMARKS=ON
cmake --build .build/stx-production --parallel 2
ctest --test-dir .build/stx-production --output-on-failure
cmake --install .build/stx-production --prefix /absolute/path/to/sdk
cmake -S stx_vorbis/tests/installed -B .build/stx-installed -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=/absolute/path/to/sdk
cmake --build .build/stx-installed --parallel 2
ctest --test-dir .build/stx-installed --output-on-failure
```

House-style review scope: first-party production adapter/wrappers, setup and
workspace selection, memory-report changes, migrated/new tests, fuzz routing,
decode CLI option, comparison tool and CMake/workflow changes. Reviewed explicit
types, named callbacks, plan/resource ownership, call-bounded borrows, failure
cleanup, conversion bounds, initialization and repeated-loop storage. The C++
spelling scan reports zero findings; it does not certify semantic compliance.
The seven pinned vendor files are unmodified and excluded from first-party
style claims. No claim is made about unrelated legacy GUI.Forms or PlaySuite
source style. Cross-platform CI outcomes belong to the new provider PR.
