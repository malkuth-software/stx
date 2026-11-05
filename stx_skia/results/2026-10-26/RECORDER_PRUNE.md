# Remove unused picture factory recording and search helpers

The retained CPU source search showed SkTSearch's only source consumer was
SkPtrRecorder.cpp. Its pointer-to-ID sets served factory recording for picture
serialization, which this library already excludes. SkBinaryWriteBuffer still
had a setFactoryRecorder option and one owning pointer, but no retained caller
used that option. SkNamedFactorySet and SkRefCntSet implementations remained in
SkFlattenable.cpp solely for this unused infrastructure.

The candidate now removes:

- SkPtrRecorder.cpp/.h: pointer-ID sets, reference-counted sets and named factory sets.
- SkTSearch.cpp/.h: the now-unreferenced generic/strided search, string search,
  ASCII lowercase owner and qsort callback-cast macro.
- The writer's factory-recorder setter, owner field and picture-only encoding branch.
- The recorder-specific implementations/includes in SkFlattenable.cpp.

This removes two implementation units from the authoritative CMake source list.
Original files remain under baseline/skia; the reference profile selects the
original SkFlattenable implementation and recorder/search units. Existing original
write-buffer sources/headers were already retained there. The five newly retained
files match the original import byte-for-byte. Candidate source/header/symbol
checks reject reintroduction of the removed machinery.

Ordinary flattenable streams still write inline type names and reuse their
existing dictionary on subsequent occurrences. No dictionary replacement or new
registry is introduced. SK_DISABLE_EFFECT_DESERIALIZATION remains enabled for the
candidate. This does not enable general effect deserialization, picture support,
image codecs or font machinery. SkWriteBuffer and the remaining SkFlattenable
implementation are still awaiting whole-file conversion; this is targeted pruning.

## Verification

M4 macOS arm64, non-Apple Clang 23.1.2 Release; isolated GUI.Forms fixture with
its pinned LLVM 22.1 toolchain. The authoritative GUI.Forms checkout is unchanged.

- All 32 tests pass in candidate and original-reference Release profiles.
- New flattenable-stream fixture: 24 cases, 3,344 bytes, hash
  16215645112662696255. The complete output matches byte-for-byte across profiles.
  Cases cover color shaders, color matrices, all four blur-mask styles, shader
  masks and dash effects; each ordinary record is checked through owned, bounded
  and direct-writer forms. Shared streams additionally exercise null entries,
  repeated type names and nested shader names.
- Growing and supplied-storage writers produce identical bytes. The candidate
  fixture also checks compile-time absence of setFactoryRecorder.
- Nine existing render/mipmap artifacts remain identical.
- Release and Debug instrumented deterministic suites: PASS. Prior diagnostic
  logs were preserved. Detailed output remains for the owner's independent
  reviewer; no new randomized review was requested or run.
- Fresh relocated installed consumer, CPU source/symbol scope and seven selected
  GUI.Forms integration tests: PASS.
- New first-party fixture: zero house-style and lifetime-check findings. The
  adapted imported implementation files are not claimed as whole-file conversions.

The standard release runner now emits flattenable.raw, and Linux x64/arm64 and
Windows workflows compare it between profiles. Their current-head results are
required after publication; local success is not a substitute.

## Costs and limits

One unused owning pointer is removed from each candidate binary writer, along
with its never-selected branch. The measured current Release writer is 80 bytes;
the original upstream-profile writer is 128 bytes, but that difference also
includes prior pruning and must not be attributed entirely to this change.
No renderer timing improvement or linked-binary size reduction is claimed.
Dead stripping could already discard some of the removed code; removing its
source/build dependencies still reduces maintained scope.

Reproduce the standalone profiles with tools/build_native.sh candidate and
baseline, then compare their flattenable.raw files. Use candidate-sanitize only
after preserving an existing integrity.log. The GUI.Forms check uses
tools/verify_gui.sh with its pinned fixture/toolchain, and candidate compile/symbol
scope uses tools/check_build_scope.py build/candidate. Original sources remain in the reference tree; relevant upstream tests remain
available under skia/tests.
