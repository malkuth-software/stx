# stx_zlib provenance

The phase-2 evidence names the original GUI.Forms source paths and hashes at
their recorded commits. The implementation moved into this package without
changing its algorithm; new code and measurements must name the current paths.

Owner authorization: after accepting the phase-1 decisions and requesting the
alternatives/leak-evidence screen, the owner authorized taking suitable material
for both PNG and deflate and rewriting it in the complete house style. This
supersedes the shortlist exclusion as a source-use restriction; references are
not thereby certified leak-free. The PNG decision now calls for an owned codec.

## Phase 2 source map

| File / material | Provenance and changes |
|---|---|
| `inflate.cpp`: canonical tree construction and symbol decoding | Derived from Mark Adler's `contrib/puff/puff.c`, version 2.3, in upstream zlib commit `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`. This is an altered C++ implementation, not original puff. Count/offset construction and canonical bit traversal retain the algorithm; resumable code state, fixed storage, explicit results, checked quotas and error handling replace puff's whole-buffer/longjmp interface. |
| `inflate.cpp`: block parser and RFC tables | RFC 1951 fields and length/distance tables, informed by the same puff reference and pinned upstream `inflate.c`/`inftrees.c`. Conservatively covered by the retained puff notice. No Chromium optimizations, Boost, miniz, zlib-ng or libdeflate source was copied into this phase. |
| `inflate.cpp`: zlib header/trailer, history ownership, quotas, streaming orchestration | Newly written GUI.Forms implementation against RFC 1950/1951 and the approved study. No heap allocation, setjmp/longjmp or exceptions. The owner subsequently requested early acceleration: the bounded bulk copy, literal-run/root-lookup and batched Adler code is newly written. Consolidation of checksum APIs and additional ISA comparisons remain in phase 4. |
| `include/stx_zlib/inflate.hpp` | Native API and explicit owner/state layout, moved from the original GUI.Forms public header. Not the eventual C ABI. |
| Tests and fuzz harness | Newly authored test logic; fixture provenance below. The test-only zlib oracle is external, pinned by the source digests in `tests/compression/oracle_sha256.json`; it is never linked into production. |

The unchanged distribution notice is in `LICENSE-puff.txt` and is installed
with the package. Do not remove it during later cleanup. The project licence
continues to apply to newly authored project material; derivation is disclosed.

## Phase 3 encoder

`include/stx_zlib/deflate.hpp`, `src/deflate.cpp` and their tests are newly
written against RFC 1950/1951 and the approved phase-1 design. The length,
distance and code-order tables describe the RFC format. LZ77 hash-chain
search, lazy matching, canonical Huffman construction and Kraft-sum repair
are implemented here without copying an external encoder. No Chromium,
zlib-ng, libdeflate or other encoder source was imported. Upstream zlib is a
comparison oracle, not a production dependency. Python's zlib runtime is an
additional independent test oracle and is reported separately from the pinned
upstream source oracle.

The speed/compression level table and shift-XOR three-byte hashing are informed
by the pinned upstream `deflate.c` configuration and hash update. The code is
newly written in the owned interface; this is an altered implementation, not
upstream zlib. Its distribution notice is retained in `LICENSE-zlib.txt`.

The sorted two-queue Huffman merge and reverse parent-depth traversal follow
the classical van Leeuwen construction discussed in Moffat and Katajainen,
[In-Place Calculation of Minimum-Redundancy Codes (1995)](https://hjemmesider.diku.dk/~jyrki/Paper/WADS95.pdf),
sections 3–4. This implementation retains explicit node records, rather than
the paper's overwritten weight/parent array; it is newly written from the
algorithm. Leaf-before-parent ties minimize the maximum depth among
unconstrained optimal trees. The subsequent Kraft repair enforces DEFLATE's
length cap; it is not claimed to be a bit-optimal length-limited solver.

The weighted Adler reduction is the same integer identity already used by
the owned inflater. Packed prefix tags, shared probe/insertion hashes,
rebased history links, bounded block preflights and locally accumulated search
effort are newly written optimizations. Later work fuses movement with rebasing,
reuses winning lookahead, infers the three-byte prefix from the tag and bucket,
locates a word mismatch with standard bit operations, and preflights match-query
and token-output bounds. A window-sized predecessor ring was measured and
rejected: it reduced storage but increased the dependent lookup cost on the
PNG comparison. The experiment is retained with the phase-3 evidence.
These optimizations contain no platform intrinsics. Endianness only
selects which end of a loaded word corresponds to the first input byte; it does
not select different tokens or stream bytes.

## PNG and later phases

For the later owned PNG codec, use libspng's progressive/bounded interface and
LodePNG's self-contained PNG/filter implementation as design references,
alongside PNG's specification. Adapt the existing first-party PNG writer to
owned deflate after phase 3. Record exact source pins, per-file notices and
altered-source markings before copying PNG code. No Boost or PNG library is
introduced as a runtime dependency by this phase.

House-style conformance improves reviewability; leak freedom, bounds and
format conformance still require the accepted executable gates. No reference's
security history transfers automatically to a rewritten implementation.

## Early optimization reference

The owner explicitly requested BFFT paradigms. The already pinned subset at
`0f75ca79fbdc9176af729fc67afed59dca436ff1` informed prepared tables, caller-owned
workspace, preflighted kernels, mode selection and bounded tails. No BFFT source
was copied into this codec. The new nine-bit table is constructed from the same
canonical code ordering described above; puff attribution remains. The
[optimization record](../results/2026-10-19-zlib/phase-two/optimization/README.md)
separates generated SIMD evidence, synthetic measurements and the PNG inspection.
