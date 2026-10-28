# Remove the disabled effect registry; convert flattenable entry points

The candidate already disabled built-in effect deserialization. Its platform
initializers were empty, but the retained core still owned a 128-entry factory
registry, a count, name sorting/lookup, a once initializer and registration helpers
throughout the effects/shaders. None of the retained GUI.Forms consumers required
manual global factory registration.

This batch removes that unused authority and converts the serialization boundary
that remains. It does not add another registry or synchronization protocol.

## Removed scope

- Public SkFlattenable::Register, NameToFactory and FactoryToName, plus their
  table, count, sorting and initialization functions.
- SkGlobalInitialization_core.cpp and SkGlobalInitialization_default.cpp from
  the candidate build. The original versions remain in the reference profile.
- Twenty-seven effect/shader registration definitions, their declarations and
  registration-only friendship. The legacy ColorShader4 registration helper is
  also removed because nothing else called it.
- The registration macro and alternate automatic-registration branch of the
  flattenable hook macro. Candidate hooks continue to report no automatic factory.

Named entries in the candidate reader now directly resolve to no factory, retaining
its existing rejection behavior. The registry's manual-registration public API is
intentionally removed; compatibility with callers using that excluded facility is
not promised. Historical type tag values and retained virtual-method declaration
order are preserved. The existing disabled-deserialization compile definition
continues to advertise policy to consumers.

The original header/implementations remain available to the reference build.
All 46 newly saved reference files were compared byte-for-byte with the original
Skia import. Reference selection restores registration code and headers together,
so the original factory initialization and reconstruction behavior remains testable.

## Converted boundary

SkFlattenable.h/.cpp now state callback borrowing, owned return data, writable
storage alignment, failure behavior and the unsupported default decoding policy.
Serialization uses explicitly initialized procedure records and typed local values.
The returned SkData owns its bytes; no callback or caller buffer is retained by
these entry points.

Bounded serialization returns zero before constructing the writer when storage
is null, empty or not four-byte aligned. Insufficient valid storage still follows
the existing writer behavior: it may write a prefix and allocate private fallback
storage, then returns zero. Null/empty decode inputs return an empty owner before
constructing a reader. These are explicit boundary corrections, not claims of a
purely mechanical rewrite. There are no new object fields or lifetime tracking.

The lower-level reader/writer, explicit private reader factory arrays and remaining
CreateProc implementations are not claimed as converted or completely pruned.
Most affected shader/effect files receive registration deletion only; that does
not convert their remaining implementations.

## Verification

M4 macOS arm64 with non-Apple Clang 23.1.2 Release. GUI.Forms uses its pinned
LLVM 22.1 fixture/toolchain. The authoritative GUI.Forms checkout is unchanged.

- Both Release profiles pass all 32 tests.
- Twenty-four serialization cases still produce 3,344 identical bytes, hash
  16215645112662696255. The fixture covers shaders, matrices, all blur styles,
  shader masks, dash effects, repeated names and three writer forms.
- The reference now explicitly reconstructs each ordinary serialized effect and
  checks its type. The candidate explicitly rejects default named-effect decoding.
  These intentionally different supported policies are tested separately.
- Candidate boundary cases check null/empty inputs, misaligned writable storage,
  zero capacity and insufficient capacity with surrounding guards. The installed
  consumer also proves that global factory registration is absent from its API.
- Nine rendering/mipmap artifacts remain identical.
- Release and Debug instrumented deterministic suites: PASS. Logs from the prior
  run were preserved before the final run. Detailed diagnostics remain reserved
  for the owner's independent reviewer; no new randomized review was run.
- Fresh relocated consumer and seven selected GUI.Forms tests: PASS. An added
  installed-fixture assertion initially lacked its explicit SkFlattenable include;
  that fixture-only compilation issue was corrected and the consumer rebuilt/run.
- Updated compile/source/symbol checks reject the removed initialization units,
  registry entry points and registration helpers.
- Converted header/implementation and changed fixtures have zero house-style and
  lifetime-check findings. The large imported files receiving deletions are not
  described as whole-file conversions.

The preceding b54984c recorder-pruning head passed native Linux x64, Linux arm64
and Windows checks. This batch needs its own current-head platform checks after
publication. No rendering-speed or linked-binary size improvement is claimed;
source/build and global-state removals are the established changes.

Reproduce with tools/build_native.sh candidate/baseline and the regular artifact
comparisons, followed by candidate-sanitize after preserving integrity.log.
Use tools/check_build_scope.py build/candidate and the relocated installed-consumer
check. GUI.Forms verification uses tools/verify_gui.sh with the pinned fixture.
