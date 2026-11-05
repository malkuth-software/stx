# Strings: corpus, prune, rewrite and fixes — 2026-10-27

The second support-code unit. The profile (`SUPPORT_CODE_PROFILE.md`) found `SkString` at 0% of
GUI.Forms' Skia render time, so this unit converts and fixes; it makes no speed claim. GUI.Forms
uses no `SkString`.

## Who uses it

A per-symbol check of the library objects (`llvm-nm` over the objects compiled for the
candidate) and a source check of the inline members found these users:
- the matrix, rect, rounded-rect and path dumps (`printf`, `appendf`, `append`, `reset`,
  `SkAppendScalar`);
- `SkReadBuffer::readString` (`set`, `reset`);
- the resource cache's trace dump (`SkStringPrintf`);
- `SkOSFile::Iter` on POSIX and Windows, which itself had no user;
- our tests.

## Corpus (f99e5c79)

`tests/string_core.cpp` writes `string-core.raw`, 3,696 cases over the members the candidate
keeps:
- **Construction:** with and without a length, embedded zeros and null.
- **Comparison:** `equals`, `==`, `!=` and `contains`.
- **Value semantics:** copies and moves over shared storage, and self-assignment.
- **Editing:** `set` and `insert` around the 4-byte allocation boundaries, on unique and shared
  strings, with offsets past the end.
- **Code points:** one to four UTF-8 bytes, a lone surrogate, and past the last code point.
- **Formatting:** `printf`, `appendf` and `SkStringPrintf` across the 1,024-byte stack buffer.
- **Scalars:** `SkAppendScalar` in decimal and hex, with infinities and NaN.

Both builds wrote identical bytes before any change.

## Prune (4f32474d)

The candidate's `SkString` keeps:
- construction, copying and moving;
- comparison and `contains(char)`;
- `set`, `insert`, `append` and code-point appends;
- `printf`, `appendf` and their `va_list` forms;
- `swap` and `SkStringPrintf`.

These had no user in the library, its Windows port or GUI.Forms, and are removed:
- **Number formatting:** the `SkStrAppend` formatters and their size constants, and the number
  and scalar insert and append members.
- **Prepending:** every `prepend` form.
- **Editing and access:** `resize`, `remove`, `operator=(const char*)`, `operator+=`, indexing and
  iterators.
- **Standard-library forms:** the `std::string` and `string_view` overloads.
- **Search:** `startsWith`, `endsWith`, `find`, `findLastOf`, `contains(const char*)`, the C
  helpers only they used, and the free `swap`.
- **Utilities:** `SkTabString`, `SkStringFromUTF16`, `SkStrSplit` and `SK_strcasecmp`.
- **Directory iteration:** `SkOSFile::Iter`, on POSIX and Windows.

`baseline/skia` holds the intake `SkString`, `SkStringUtils`, `SkOSFile.h` and both OS file ports
(from 490c9bd4), and the reference build compiles them. `string_core.cpp` checks by expression
that the removed members are absent from the candidate and present in the reference. CI
compiled the Windows port without `Iter` (both workflows passed at 4f32474d).

## Mechanical rewrite (d33980b3)

- **Spelling:** member access through `(*p)`, named casts, and initialized locals and members.
- **Helpers:** the C-string helpers become `inline`, and the `SizeOfRec` macro becomes a
  function.
- **Offsets:** `append`'s `(size_t)-1` offset becomes the private constant `kEnd`.
- **Includes:** trimmed. `SkString.h` no longer pulls in `SkScalar.h`, `<string>` or
  `<string_view>`; a scan found no includer relying on them.
- **Ports:** the OS file ports drop the includes that only `Iter` needed.

**Result:** `string-core.raw` stayed identical. The stack buffers for `printf` are now
zero-initialized, at the cost of a 1 KB clear per call; output is unchanged.

## Fixes

| Defect | Before | After |
|---|---|---|
| Inserting the string's own text into itself | In place, the tail moved before the text was copied: `"ab".insert(0, c_str() + 1, 1)` gave `"aab"`, not `"bab"`, and the copy could overlap | Text from the string's own storage takes the allocating path, which reads it from the old storage |
| Setting a string from its own text | `memcpy` between overlapping ranges | `memmove` |
| `printf` with the string's own text as an argument | Over 1,024 bytes, the string was resized before formatting: either freed (use after free) or written while it was read | Formats into a separate string, then moves it in |
| Results over 4 GiB | `set` and `insert` silently truncated. `insert`'s check could wrap where `size_t` is 32 bits (wasm), so it then copied a huge length | Release checks, written so that they cannot wrap |
| A null text with a length | The constructors and `set` gave uninitialized bytes, or kept the old ones. `insert` and `equals` read through null | The constructors and `set` give zero bytes. `insert` and `equals` stop the program |
| `vsnprintf` capacity | `outLength + 1` overflowed `int` at the largest length | Computed in `size_t` |
| `decltype(fRec)` in a static assertion | — | Spelled `sk_sp<Rec>` (missed by the rewrite) |

**Contract.** The header now states the contract: at most `UINT32_MAX` bytes, what a null text
with a length means, and that texts may come from the string being edited.

**Checks (candidate only; the intake code fails or is undefined on these inputs).**
- **Own-text edits:** every insertion and replacement of a piece of a string's own text into
  itself, on unique strings of up to 20 bytes, matches `std::string`.
- **Own-text formatting:** `printf` and `appendf` with the string's own text as an argument
  give the right result at 10, 500, 1,100 and 3,000 bytes.
- **Null text:** a null text with a length gives zero bytes, from the constructors and from
  `set` on unique and shared strings.
- **Stop tests:** `string_core_stops_insert-null`, `-equals-null`, `-insert-too-long` and
  `-set-too-long`.

**Negative control.** Against the unfixed code (d33980b3), all three checks fail: edits=0,
formats=0, zeros=0. The stop tests also pass there, because the unfixed code crashes on those
inputs. `cmake/expect_stop.cmake` accepts any signal, so these tests show only that the program
does not continue, not that a Release check stopped it.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 95/95, including the four stop tests.
- **Reference:** 83/83.
- **Corpora:** `string-core.raw` is identical in both builds at every step, and all shared
  artifacts are identical apart from the six intentional differences. No warnings.
- **Instrumented suite:** PASS.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9 with `tools/verify_gui.sh`.
- **Style:** the lexical checker finds nothing in the string files.
- **Inventory:** the AST inventory falls from 619 excluded constructs in 53 files to 501 in 51.
  In the kept string code no construct remains; the POSIX port keeps two arrows, which belong
  to its own unit.

## Not done

- **Windows and Linux:** CI covers the fixes on push.
- **OS file port:** `SkOSFile` and the ports are otherwise unconverted. Only `SkData`'s file
  loaders use them.
