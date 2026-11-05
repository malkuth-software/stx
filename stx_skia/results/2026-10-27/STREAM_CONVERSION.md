# Streams: prune, corpus, rewrite and fixes — 2026-10-27

The first support-code unit. The owner chose to prune the unused stream classes and convert
the rest. The profile (`SUPPORT_CODE_PROFILE.md`) found streams at 0% of GUI.Forms' Skia
render time, so this unit converts and fixes; it makes no speed claim.

## Prune (1def781c)

The candidate keeps only the abstract interfaces that something uses:
- **`SkStream`:** `read`, `skip`, `isAtEnd`, position and length, for
  `SkData::MakeFromStream`.
- **`SkWStream`:** `write`, `flush`, `bytesWritten` and `writeText`, for `SkPath::dump` and the
  write buffers.
- **`SkStreamPriv::RemainingLengthIsBelow`,** for `SkData`.

These had no user in the library or in GUI.Forms, and are removed:
- the memory, file and dynamic-memory streams, and `SkNullWStream`;
- the rewindable, seekable, asset and memory hierarchy, and `MakeFromFile`;
- typed readers, text and packed writers, and `writeStream`;
- the other `SkStreamPriv` helpers, and `SkMD5`.

Only our own tests used them. Tests that used a concrete stream as a sink now write into
`tests/stream_sink.h`'s `ByteSink`. `tests/data.cpp` checks by expression that the removed
members are absent from the candidate and present in the reference.

`baseline/skia` holds the intake `SkStream.h`, `SkStream.cpp` and `SkStreamPriv.h` (from
490c9bd4) and `SkMD5`, and the reference build compiles them.

## Corpus (5c510650)

`tests/stream_core.cpp` writes `stream-core.raw`, 778 cases:
- **Remaining length:** `RemainingLengthIsBelow` for every combination of known or unknown
  length and position, with lengths, positions and requests around 0, 1, 7, 64 and `SIZE_MAX`;
- **Skip:** `SkStream::skip`, with position and end state afterwards;
- **Text:** `SkWStream::writeText`, with the bytes it wrote.

Both builds wrote identical bytes before any change.

## Mechanical rewrite (6e053cb8)

- **Includes:** trimmed to what the kept code uses (`SkTypes.h`, `<cstddef>`, `<cstring>`).
- **Spelling:** member calls through `(*this)`, and a defaulted `SkWStream` destructor.
- **Result:** `stream-core.raw` is identical to the pre-rewrite output and to the reference.

## Fixes

| Defect | Before | After |
|---|---|---|
| `writeText(nullptr)` | `SkASSERT` only, so Release called `strlen(nullptr)` | Writes nothing and returns `false`. The stream stays usable |
| Null stream in `RemainingLengthIsBelow` | A pointer with `SkASSERT` only, so Release dereferenced null | Takes `const SkStream&`, so the type states the contract. The only caller, `SkData::MakeFromStream`, already refuses a null stream |
| Position past the length | The intake subtraction wrapped to a huge remainder, so every request looked satisfiable | Already fixed in 34b51825: nothing remains, so every length is below it. The header comment now says so |

**Checks.**
- **Candidate only:** a null text is refused, writes no bytes, and a later write still works;
  a position past the length is refused for 0, 1 and `SIZE_MAX`.
- **Reference:** these checks are excluded there, because the intake code is undefined at both
  inputs. The corpus calls the intake helper with a pointer and the candidate's with a
  reference.

**Correction.** The first fix commit (11292189) kept the pointer and made a null stream a
Release check, with a stop test. It gave as the reason that the reference build compiles the
candidate's `SkData.cpp`. That was wrong: the reference compiles `baseline/SkData.cpp`, the
intake `SkData`, so the candidate's helper can change alone. The follow-up commit switches to
the reference parameter and drops the stop test.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 91/91 at the first fix commit, with the stop test. After the correction, which
  removes it, 91/91 again, now counting the string corpus added meanwhile.
- **Reference after the correction:** 83/83, with the string corpus.
- **Reference:** 82/82.
- **Corpora:** `stream-core.raw` is identical in both builds, and all shared artifacts are
  identical apart from the six intentional differences.
- **Instrumented suite:** PASS.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9 with `tools/verify_gui.sh` (pinned LLVM 22.1.8).
- **Style:** the lexical checker finds nothing in the five stream files.
- **Inventory:** the AST inventory falls from 801 excluded constructs in 58 files to 619 in 53,
  across the prune and the rewrite. The intake `SkStream.cpp` alone had 120 member arrows.

## Not done

- **Windows and Linux:** run by CI on push, not locally.
