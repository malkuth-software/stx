# Retained core bounds and stream review

The Codex Security Standard scan at source commit
`544d24e0932b9194180557b88f8b9375bb405eb8` reviewed selected paths under
`skia/src/core`. Its scan ID is `22cb8ae0-3021-4678-aef6-7da4f5b26517`.
It reported four findings in three operations. Coverage is partial: reviewers
fully inspected named parser, pixel, cache and geometry files, not all 339
tracked core files. It does not establish a library-wide security clearance.

| Operation | Defect and repair | Reachability |
| --- | --- | --- |
| `SkBase64::Decode` | The inner loop read beyond the explicit input extent and could write a fifth symbol into a four-byte stack array. The decoder now checks every input read, completes each quartet within its four slots, validates padding before writing, and reports zero length on failure. | Compiled private decoder; no retained core caller or installed declaration was found. Source-tree integrations could call it. |
| `SkWStream::writeStream` | The copy discarded the byte count returned by `read`, then wrote uninitialized scratch bytes. It now writes and advances by the actual count, continues after positive short reads and fails at early EOF or an invalid count. | Public stream API; exposure requires a source that ends or fails before the requested copy length. |
| `SkBitmap::extractAlpha` | A padded filter mask could provide `height * rowBytes` bytes while the bitmap allocation reserves only the active final-row pixels. The copy now uses the destination allocation extent and checks the source extent. | Retained blur and shader filters return tight rows. A source-tree custom padded filter can exercise the path. |

The new candidate-only `core_bounds_contract` covers exact-size Base64 input,
multiple quartets, padding errors, whitespace, positive short reads and early
EOF. `mask_filter_contract` uses a padded custom filter at widths 1, 2 and 3
to verify the extracted active pixels. On the M4 Mini the candidate release
suite passed 50/50 tests, the reference release suite passed 49/49, and the
candidate sanitizer runner reported only `PASS`. The paired render-artifact
comparison passed with identical output. The render timing trial is not a
claim about the performance of the modified functions. A fresh-prefix installed
consumer also compiled, linked and ran.

The review also raised an extreme-coordinate clip translation question.
Rechecking the branch inequalities showed the suspected narrowing and
subtraction values stay within signed `int` whenever those branches execute.
No change to clip translation is justified by that candidate.

## Follow-up stream contract repair

`SkData::MakeFromStream` previously rejected a positive short read even though
`SkStream::read` returns an actual count rather than promising to fill the
request. It now accumulates exactly the requested number of bytes, accepts
successive positive short reads, and returns null on early EOF. The preflight
for a length-reporting stream also rejects a reported position beyond its
total length before subtracting the two quantities. `data_contract` covers a
one-byte-at-a-time stream, early EOF, inconsistent position metadata and a
null source. The candidate and reference release suites again passed 50/50
and 49/49 respectively; the candidate sanitizer runner reported `PASS`.

`SkMemoryStream::move` also added a signed offset to an unsigned cursor. A
negative move could wrap to a large value and clamp at the end instead of
moving backward. It now handles backward and forward movement separately,
including the minimum `long` value and both stream boundaries. `data_contract`
checks those cases for candidate and reference builds.

An empty `SkMemoryStream` now returns zero from `read` before forming an
address from its null byte pointer, and `getAtPos` returns null directly.
`data_contract` checks the empty-stream result and unchanged output byte.

The block-backed stream returned by `SkDynamicMemoryWStream::detachAsStream`
had the same signed/unsigned move error. It now clamps backward and forward
moves within the stream, including the minimum `long` offset. Its read count
is limited by subtraction from the remaining length, so a huge request cannot
wrap `position + count`; a zero-byte read returns immediately. Absolute seeks
past the end clamp to the end as the public stream contract specifies.
`SkDynamicMemoryWStream::read` now validates `offset` and `count` separately
against its written length before copying, preserving the documented
no-partial-write failure result. Candidate-only `data_contract` cases check
these boundaries. The M4 candidate Release suite passed 50/50 tests, the
candidate sanitizer runner returned `PASS`, and ten candidate/reference render
and serialization artifacts matched byte for byte.

`SkDynamicMemoryWStream::write` also aligned a requested block length before
checking whether the alignment and block header fit in `size_t`. A near-maximum
count could wrap to a small allocation size. It now rejects that count and a
total stream-length overflow before writing into the existing tail or
allocating a new block. `data_contract` checks that the rejected write leaves
the stream unchanged. The candidate Release suite again passed 50/50, the
sanitizer runner returned `PASS`, and the ten reference artifacts matched.

A later public constructor case supplied a null `FILE*` with a nonzero size to
`SkFILEStream`. Its invalid object claimed a nonempty range, so `isAtEnd()`
could ask for the size of a null file handle. The shared constructor now gives
any null handle an empty range. `core_bounds_contract` checks the invalid
stream, its read, end/length results and duplicate. The candidate Release
suite passed 50/50, the reference suite passed 49/49, the instrumented suite
returned `PASS`, and the recorded rendering hashes remained identical.

Filtered alpha extraction had two more boundary cases. The metadata pass
aligned a filter-supplied width before checking whether it fit in signed
`int`; a near-maximum width could overflow that alignment. The pixel pass
could return failure after allocating a destination mask, while the previous
cleanup owner was constructed only on success. The extraction path now owns
every returned mask image before interpreting the filter result and checks
the filtered width before alignment or bitmap setup. A candidate-only custom
filter supplies oversized bounds in either pass; the existing failing filter
also exercises the allocation-then-failure fallback through `extractAlpha`.
The candidate Release suite passed 50/50, the reference suite passed 49/49,
the instrumented suite returned `PASS`, and the recorded rendering hashes
remained identical. The reference build shares this imported bitmap path, so
its result is a compatibility comparison, not an unfixed control for this
specific repair.
