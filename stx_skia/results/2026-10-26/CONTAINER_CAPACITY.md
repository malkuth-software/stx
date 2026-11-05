# Alignment and container capacity

SkAlign.h, SkContainers.h and SkContainers.cpp are converted. Their original
versions are preserved byte-for-byte from import commit
`490c9bd4a97884d268a5b3f9924748ebf31954bb`; the reference source list selects the
original container implementation and its include priority selects the headers.

## Preserved policies and bounds

Alignment helpers retain their arithmetic and unsigned wrapping behavior. Some
format-validation callers rely on comparing the rounded result against the input;
turning wraparound into an abort would change that behavior. Signed alignment
inputs must be nonnegative with representable rounding additions. Alignment
helpers do not substitute for checked allocation-size arithmetic. Compile-time
pointer-width dispatch and named results make the existing operations explicit.

SkContainerAllocator retains its two immutable fields and 16-byte M4 layout.
Construction validates that the maximum element count fits the element-size byte
product. Allocation validates count and finite growth factor at its boundary.
No capacity metadata, allocation header, registry or per-element checks are added.

Runtime growth still rounds element counts in groups of eight and caps at the
configured maximum. Inline RoundUp<T> still rounds bytes to eight, then divides
back into elements; these are deliberately distinct policies. Its compile-time
size product and padding are bounded before arithmetic.

Growth multiplies in double precision as before, but now compares the result
against the configured maximum before converting it to an integer. An enormous
finite factor can produce infinity; that result is clamped without an invalid
floating-to-integer conversion. Ordinary truncated and rounded results remain
unchanged. NaN, infinite factors and factors below one are outside the admitted
allocation contract and are rejected at the public boundary.

Returned spans carry an allocation pointer and its usable byte extent; callers
still release that allocation with sk_free. The fatal byte-allocation entry
returns empty for zero. The fallible entry retains the original implementation's
minimum-size allocation behavior for zero, now documented explicitly.

## Verification

Sequential M4 macOS arm64 builds use non-Apple LLVM 23.1.2:

- Both candidate and reference Release profiles pass all 42 tests.
- 4,161 alignment cases cover regular values, pointer alignment, non-power-of-two
  rounding, inline byte rounding and unsigned values near the maximum.
- 30,780 ordinary growth cases check bounds, monotonicity and rounding. Their
  complete uint64 result trace is identical byte-for-byte: 246,240 bytes, with
  recorded fingerprint 7262172567427146524. The fingerprint supplements the
  actual file comparison; it is not the only comparison performed.
- Candidate-only cases check enormous finite growth factors clamping at the
  configured maximum. The reference is not asked to perform its formerly
  out-of-range integer conversion.
- 590 allocations check zero behavior, minimum storage, usable extent and
  alignment. Candidate and reference allocator objects both measure 16 bytes.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Final local records are `build/container-capacity-trace-gates.log`,
`build/container-capacity-gui.log`, and archived
`build/container_capacity_trace_gate.sh`. Earlier ordinary/fingerprint gate logs
are retained separately. The final runner compares both generated
`build/{candidate,baseline}/container-growth.raw` files on the build host.
Instrumented diagnostics remain reserved for the owner's independent reviewer.
All converted sources and the fixture pass house-style and lifetime spelling
checks. No speed improvement is claimed. The element containers and broader
renderer still require conversion.
