# Implementation review and proof obligations

Status: source-reviewed invariants and executable checks; **not** a machine-checked
formal proof and not a claim that this new codec is free of vulnerabilities.

## Parsing and ownership

1. BitReader maintains `position <= 8 * byte_count`; the multiplication is checked.
   Reads validate their entire width before mutation. Width is at most 64 and
   splitting avoids shifts by the integer width. The fuzz oracle reconstructs
   individual bits independently and checks failure atomicity.
2. An Ogg page becomes visible only after its 27-byte header, at most 255 laces,
   bounded body and CRC are available. No page-body access precedes those checks.
   Input capacity is reserved once. A current page prevents compaction, so packet
   spans remain valid through push; next_packet/reset ends that borrow.
3. Continued packet assembly is bounded by `packet_bytes` and the global resource.
   A page-contained packet uses a borrow; an assembled packet transfers vector
   storage to the output owner. Sequence gaps discard incomplete assembly.
4. Codewords are constructed in entry order. Kraft bounds, prefix conflicts,
   leaf/branch conflicts and the single-entry exception are explicit. Runtime
   traversal only follows validated node indices; an absent branch is an error.
   Prefix-table entries contain their actual consumed bit lengths.
5. Setup verifies every book/floor/residue/mapping/mode reference before publishing
   `unique_ptr<const Setup>`. Every VQ book has positive dimensions, finite expanded
   values, checked product sizes and a validated Huffman structure. Referenced
   books cannot be empty; unused empty books are permitted. Floor-1 X coordinates
   are unique, and neighbor indices always refer to earlier points.
6. Floor-1 Y reconstruction checks its range before inverse-dB indexing. Residue
   bounds clamp to spectrum extent; partitions are whole bounded regions. Type 0
   uses column/row scatter inside a partition. Type 2 maps encoded interleaving
   directly to owned planar spectra. No temporary transpose is required.
7. Decoder setup, metadata and workspace allocations flow through a counting
   memory_resource. Root objects are charged explicitly; nested roots are charged
   through the parent's resource. Allocation failure releases partial construction.
   Fault-injection tests fail each allocation ordinal until a complete decode
   succeeds and require zero outstanding allocations after every attempt.
8. Only one PCM block and one event are pending. Repeating advance cannot overwrite
   an outstanding output borrow. Partial consumption moves the live start without
   allocating. Chain transitions release the prior immutable setup; workspace
   capacities may be retained and are reported as such.
9. Exceptions are contained at decoder/demux/C boundaries. Expected streaming
   alternatives remain explicit status values. Fatal failures suppress output and
   stay sticky until reset. Recovery drops invalid overlap and marks the timeline.

## Numerical contract

The unnormalized Vorbis inverse MDCT is

`x[t] = sum_k X[k] cos(pi/M * (t + 1/2 + M/2) * (k + 1/2)), M=N/2`.

Expanding this angle gives input modulation, a positive N-point FFT and output
modulation. Plans store bit-reversal indices, stage-major contiguous twiddles,
modulation coefficients and windows. The portable butterfly is the reference for
SIMD kernels; a separate direct cosine-sum test validates the transform identity.
No fast-math or contraction is enabled. Scalar and SIMD operation order matches.
The tests require `1e-12` absolute scalar/SIMD transform agreement and `1e-9`
absolute agreement with the direct definition for their bounded test spectra.

The generated libvorbis differential acceptance bound is
`2e-6 + 2e-6 * abs(reference_sample)` for float32 output, with exact sample counts.
Historical floor-0 material is separately reported because libvorbis/FFmpeg use
float approximations where this implementation retains doubles. int16 conversion
has an explicit saturation/rounding contract; Tremor is an independent fixed-point
comparison, not a bit-exact specification of that rounding.

## House-style review scope

Reviewed scope: all C++ implementation/public headers/tests under `stx_vorbis`, C
adapter declarations and C tests, corpus/fuzz/differential tools, and standalone
CMake/workflow. No `auto`, lambdas, trailing returns, structured bindings, coroutines,
arrow member access, or ranges pipelines are introduced. Existing provider code,
vendored oracle code, and the sibling's threading implementation are outside this
review. The repository spelling scanner runs on this directory; it supplements
source review of ownership, access extents, conversions, initialization, loop
storage, error states and operation order.

SIMD selection happens before repeated processing. Scratch owners belong to the
decoder; there are no globals holding mutable audio state. Intrinsics are confined
to the synthesis translation unit. The mathematical layout follows the requested
BFFT discipline (explicit types, real/imaginary SoA, planned coefficients, direct
contiguous kernels); no BFFT source is copied or linked.

## Remaining assurance

Fuzz runs are finite and corpus coverage is incomplete. There is no formal proof
of the complete setup parser, Huffman builder, packet pipeline, allocator boundary,
or C adapter. Longer fuzz campaigns, independent security review, and additional
real-world malformed/multiplexed broadcast recordings remain useful before making
this the default decoder for untrusted application input. Tests and C++20 ownership
are evidence, not substitutes for that work.
