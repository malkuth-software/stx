# Support leftovers: UTF, buffers, template and numeric helpers — 2026-10-27

The owner asked for the first four groups of the remaining support code before handing over to
the GUI.Forms sibling sessions. Every API is kept whole. Each group followed the same order:
- a corpus first;
- then a verified-identical respelling;
- then fixes with candidate-only checks.

Before each change, the reference build gained intake copies of the files involved, in
`baseline/skia`.

| Group | Corpus | Respelling | Fixes |
|---|---|---|---|
| `SkUTF` | 88493b16 | b812904e | ec58e62d |
| `SkRBuffer`, `SkWBuffer`, `SkDataTable` | 4868cee1 | 06b06e9e | 98a24557 |
| Template helpers | 8dce25b6 | 2b7d4ad3 | 889c84cc |
| Numeric helpers | f3c7b7bd | 4472e01a | 36377535 |

## 1. SkUTF

**Corpus.** `utf-core.raw` has 1,005 cases, identical in both builds. It covers counting,
decoding, encoding and conversion into buffers of every capacity, over:
- valid text, built with an independent encoder;
- structurally malformed text.

**Intentional difference: `utf-core-strict.raw`.** It holds well-formed input that is not
Unicode. It matched before the fix and differs after, on purpose. The string corpus's
lone-surrogate case moved there.

**Fixed:**
- **UTF-8 decoding follows Unicode Table 3-7.** Overlong forms (`E0 80 80`), encoded surrogates
  (`ED A0 80`) and values past U+10FFFF (`F4 90 80 80`) were accepted and are now rejected.
- **UTF-32 accepts only scalar values.** Surrogates and anything up to 0xFFFFFF were admitted
  before. `CountUTF32` also read through a null pointer given with a length.
- **The encoders return 0 for lone surrogates,** as their documentation promises for invalid
  code points.
- **The counts and conversion lengths stop at `INT_MAX`,** and a negative capacity is refused.

**Negative control.** The strict checks fail against the unfixed sources.

## 2. SkRBuffer, SkWBuffer and SkDataTable

**Corpus.** `buffer-core.raw` has 531 cases. It covers write plans replayed into measuring,
unbounded and sized writers; reads of every length; and every data-table factory.

**Fixed:**
- **Sized-writer bounds.** A sized `SkWBuffer` checked its end only with a debug assertion, and
  `padToAlign4` not at all. Both now stop at the end. The position is an offset, so measuring
  mode no longer does arithmetic on a null pointer.
- **Null destination.** `SkRBuffer::read(nullptr, n)` copied to null; it now skips. A reader
  given null data with a length stops.
- **`SkDataTable`:**
  - indexes are checked in Release;
  - the factories compute sizes with overflow checks (wrapped sizes copied past the
    allocation);
  - null sources and an element size of 0 are refused (the table read its storage as a
    directory).

**Negative control.** Against the unfixed code, the null-destination read crashes (exit 139). Six
of the seven new stop tests run past the violation; the seventh crashes in the copy.

**Not changed.** `SkRBuffer::skipToAlign4` aligns the absolute address while the writer pads by
offset. Path deserialization reads points through pointers into the buffer, so it already
needs 4-byte-aligned buffers, and the two agree there.

## 3. Template helpers

**Corpus.** `template-core.raw` has 187 cases, covering `SkZip`, `SkTLazy`,
`SkTCopyOnFirstWrite`, `SkEnumBitMask`, `SkTPin`, `SkTFitsIn`, `SkTo`, the saturating helpers,
`SkSpanPriv` and `SkCount`.

**Fixed:**
- **`SkZip`'s converting constructor** was constrained with `enable_if` but no `::type`, which
  never constrains. A zip over a derived type converted to one over a smaller base, which then
  indexed with the wrong stride. The test fails to compile against the old header.
- **`SkZip` lengths and indexes** are now checked as `SkSpan` checks them. `SkMakeZip` refuses
  collections of unequal length in Release.
- **`SkTPin`** returns a copy, where a reference to a temporary argument could dangle.
- **`SkAbs32(INT32_MIN)`** saturates to `INT32_MAX`. The old code negated it, which is undefined
  and in practice gave a negative value. The scan converters call it on saturated differences.
  It saturates rather than stops, because stopping would turn extreme geometry into a crash.
- **`SkTLazy` and `SkTCopyOnFirstWrite`** stop instead of reading an empty value or a null
  source.
- **`SkSpanPriv::Copy`** checks its sizes in Release.

**Kept:** `SkZip` keeps one `decltype`. A container's `data()` may be overloaded on `const`, so
nothing else names the element type.

## 4. Numeric helpers

**Corpus.** `numeric-core.raw` has 268 cases, covering the conversions, rounding, NaN and
finiteness tests, fixed point, alpha multiplies, ULP comparisons, `SkFloatingPoint` and
`SkFloatInterpFunc`. It sticks to inputs where both builds are defined.

**Fixed:**
- **`SkIsFinite(array, 0)`** no longer reads an element.
- **`sk_double_*2int`** saturate.
- **`SkFixedDiv` by zero** saturates to the numerator's sign.
- **Fixed-point rounding and averaging** compute in 64 bits; rounding past the top saturates.
- **`SkDoubleToFixed`** is exact over the whole `int32` range and saturates beyond it.
- **`SkFloatingPoint`** copies its bits with `memcpy` instead of reading a union.
- **`SkFixed.h`** includes `SkTo.h`, which `SkFixedDiv` uses.

**Corrected during the unit.** My first `SkDoubleToFixed` reused `sk_double_saturate2int`, which
clamps at `-INT32_MAX`, and changed the in-range result for -32768.0. The corpus caught it. The
version committed is exact over the full range.

**Not run against the unfixed code:** the out-of-range checks. Every one of those inputs is
undefined behaviour in the old code.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 123/123.
- **Reference:** 90/90.
- **Corpora:** the four new artifacts are unchanged by every respelling and fix, apart from the
  intended `utf-core-strict.raw`. All other shared artifacts are unchanged. No warnings.
- **Instrumented suite:** PASS after each fix.
- **Installed package:** passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9.
- **Inventory:** the AST inventory falls from 276 excluded constructs in 35 files to 249 in 32.

## A build pitfall found on the way

**What happened.** A negative control restored the fixed sources within the same second as the
build of the unfixed ones. The mirror keeps file times, so ninja saw nothing newer and kept the
stale objects. The next run reported a failing test from the old code.

**Effect.** The committed results were unaffected: they came from gates run before the control.

**Rule.** After restoring files, `touch` them before building again.
