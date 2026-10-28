# Shared gradient storage, interpolation and pipeline setup

The shared gradient base now names stop/color views, initialized state,
interpolation buffers, conversion plans and arena-backed pipeline contexts.
The inline/heap byte owner precedes its borrowed pointers. Allocation dimensions
use checked addition/multiplication, array counts are checked before narrowing,
and evenly spaced stop traversal uses an integer interval count while retaining
the existing float interpolation scale. Unrepresentable internal allocation
requests follow the existing terminating allocation policy; no new error protocol
is introduced.

Metadata/stop views reject temporary gradient objects. SkGradientScope documents
that its returned spans borrow its buffers and rejects temporary scopes. The
color transformer cannot be copied or moved with its self-referential position
pointer. It counts extra stops for powerless interior hues during conversion and
reserves the resulting color/position storage before generating those stops.
The copy of a neighbor's hue is a named operation. ProPhoto's one-time matrix
initialization uses a named function with an explicit matrix reference. Pipeline
stage ordering and numerical color formulas remain unchanged.

## Removal scope

The bitmap cache is documented in the original header as Graphite storage for
colors/offsets exceeding its uniform limit. No retained CPU implementation reads
or writes it. The explicit-position override is documented as a Ganesh requirement;
the single retained transformer caller never enables it. Those implementations,
and an unused span-construction macro, are removed under the owner's CPU-only
scope. The original implementation is preserved in the reference overlay.

This is not a claim that CPU gradient caching could never be useful. Absence of
callers is not sufficient to remove capabilities within the intended CPU scope.
Explicit uneven positions, hard stops, duplicate stops and CPU interpolation
remain supported and exercised. A future CPU cache would require its own evidence.

## Verification

The previous geometry fixture now has 34,200 renders: nineteen geometries, four
tile modes, five color/position layouts, all fifteen interpolation spaces, both
premultiplication choices and three local matrices. The four hue methods are
paired with the four tile modes, not tested as an independent Cartesian product.
Layouts include single and two-color fast paths, four-color implicit/explicit
stops, and nine-color heap storage with achromatic colors, transparency and
repeated endpoint/interior stops. The corpus is 103,831,200 bytes.

A new reference source snapshot ensures these shared routines are independently
compiled from the original implementation, rather than inadvertently comparing
two builds of the converted base. Existing baseline headers remain untouched.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 59/59 PASS.
- Reference release: 58/58 PASS.
- All sixteen common raw artifacts match byte for byte.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- Converted source/header and expanded fixture: zero house spelling/lifetime
  findings, supplemented by manual ownership, arithmetic and stage-order review.

No timing improvement is claimed. The interpolation reserve changes reduce
opportunities for growth during stop emission but have not been benchmarked.
Large numerical cases beyond the real-buffer corpus are not established by the
pixel comparison. Effect decoding remains excluded; compatibility parsing bodies
are retained and compile, without claiming a supported serialization interface.
Public gradient descriptors, raster stage kernels and the wider canvas/geometry
conversion remain separate work.
