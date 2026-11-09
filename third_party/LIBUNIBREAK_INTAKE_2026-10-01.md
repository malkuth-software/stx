# Private Unicode line-break dependency intake

**OBSERVED source:** libunibreak 8.0, tag `libunibreak_8_0`, commit
`28a2756b864c343f438cd22537d49d394d4666a5`, from
<https://github.com/adah1972/libunibreak>. Upstream NEWS and source identify
Unicode 17.0, UAX #14 revision 55. The supplied LineBreakTest header identifies
17.0.0. Retain the upstream `LICENCE` and Unicode data notices, with the existing
Unicode license in `third_party/unicode/LICENSE.txt`. No vendor source is edited
or claimed to meet the first-party coding style.

Exact upstream Git blob identities (independent of local checkout CRLF policy):

| File | Blob SHA-1 |
|---|---|
| `LICENCE` | `6b4137ca2158ffb57a112d54726dd11b6a2a06db` |
| `src/linebreak.c` | `e15e4c26036c3ce3baa4d510d9e36586b9b73583` |
| `src/linebreak.h` | `30c00ef4fe9e48941741ecdd75d3e1a63fae098a` |
| `src/linebreakdata.c` | `3785340cca80613b2cb7ff3026e8b252bbdf533d` |
| `src/linebreakauxdata.c` | `291730879833592d5fe3cb6d50ce82496d2040dc` |
| `src/LineBreakTest.txt` | `7ceac7ef136a7d768379cecf0562962d83b767a5` |

**MEASURED:** coordinator compiled the untouched upstream nine library C files
and `tests.c` with GCC 16.2, C99, `-O2 -Wall -Wextra` on Shadow Windows. Supplied
line conformance passed **19,338/19,338**, word **1,944/1,944**, grapheme
**766/766**, with no skips. `test_skips.h` contains only terminators. An upstream
unused-variable warning in `tests.c` is retained; no vendor patch was applied.
These are conformance results, not a throughput benchmark or proof of the new
wrapping adapter. Intake source and executable are under
`.build/libunibreak-8.0-intake/`.

**OBSERVED API/storage review:** `set_linebreaks_utf8` writes one byte per input
byte to caller-owned output. Decisions describe breaks after code-point-ending
bytes; interior bytes carry `LINEBREAK_INSIDEACHAR`. Use `"-strict"` for the
tested default Unicode behavior, independent of host locale. End-of-input
`LINEBREAK_INDETERMINATE` needs explicit adapter interpretation. The input is
validated UTF-8, at most 16 KiB, and the output belongs to charged worker storage.
The production source has no heap allocation calls; its line-break state is
local and property/action tables are static. `init_linebreak` is a no-op.
The wrapper must preserve LF/CRLF/source offsets and intersect legal boundaries
with extended graphemes and actual shaping clusters. This library does not
choose widths, fonts, bidi order, hyphenation, ellipsis or dictionary services.

**OBSERVED coordinator acceptance:** admit this pinned private dependency for
the already assigned development text-mask profile. A first-party UAX #14 rewrite
would add algorithm/table maintenance without a demonstrated need; existing
whitespace-only wrapping cannot meet that profile. This is a reversible private
dependency choice, not a new public API, installed SDK promise or global ADR.
Keep it out of normal OFF builds. First-party fetch/build/adapter code receives
house-style review; upstream code and data retain their original licenses.

**OBSERVED owner decision, 2026-10-26:** the admission widens from the
development text-mask profile to the text engine ("libunibreak is fine, can
admit it"). Stage L's `TextBlock` wraps at UAX #14 opportunities through the
text engine's `TextMetricsProvider`, and a control cannot otherwise reach the
breaker ([`planning/stages/L-layout/PLAN.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/L-layout/PLAN.md), section 12.9). From that date:

- `gui_forms_linebreak` is built wherever `GUI_FORMS_ENABLE_HARFBUZZ_TEXT` is
  on (`cmake/LineBreak.cmake`, formerly `cmake/TextMaskLineBreak.cmake`), and
  `gui_forms_text_engine` links it privately;
- `fetch_linebreak.sh` runs wherever `fetch_text_stack.sh` runs: by
  `tools/ci_build.py` on every host, and by the release workflow;
- `gui_forms_linebreak_conformance` runs in the default text build;
- the pin, the licence and the attribution are unchanged, and builds without
  HarfBuzz text still have no line-break dependency.

The sentence above, "Keep it out of normal OFF builds", now reads as builds
without HarfBuzz text. The text-mask profile keeps its own adapter.

`fetch_linebreak.sh` fetches the exact commit and refuses an unexpected origin,
modified checkout or non-repository destination. Until 2026-10-26 it was
requested only for the development mask build; it now runs with the
general text-stack fetch. Initial
manual invocation with an incomplete MSYS PATH exposed a nested `dirname`
failure: the script incorrectly resolved to the working directory. The script
now resolves its parent in a separate checked assignment. The newly fetched
checkout was verified and moved to the exact intended `third_party/libunibreak`
path; no user data was removed. The two local Git implementations also disagreed
on inherited CRLF policy; an explicit matching local Git setting resolved that
false dirty report. Normal MSYS CI uses one Git/tool environment throughout.

The corrected script passed `bash -n`, idempotent exact-pin fetch and a deliberate
missing-`dirname` PATH check: it failed before creating any destination. Its
authored scope was reviewed for named steps, failure ordering, quoting, origin/
revision verification and preservation of existing modifications under the
language-neutral house style. The vendor tests and sources remain upstream code.
