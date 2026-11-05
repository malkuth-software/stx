# HarfBuzz 14.6.0 intake and GCC comparison warning

**GIVEN:** update the imported HarfBuzz to the newest release, compile it, and
explain the memory-operation warning if it remains.

**OBSERVED:** the latest stable upstream release on 2026-10-06 is
[14.6.0](https://github.com/harfbuzz/harfbuzz/releases/tag/14.6.0), published
2026-10-05. The pin changes from 14.2.1
(`56feae4035bdd48f62ba2b8d8c16232d4d89b3a4`) to 14.6.0
(`c7a7457b7385f33178e8cf87615ca077a810bbe7`). The Mac renderer's diagnostic label
is updated too. FreeType and Skia stay at their existing pins. Historical
measurement records retain the dependency versions actually measured.

The upstream checkout is unmodified. GUI.Forms still disables HarfBuzz raster,
vector, GPU, subset and utility targets. The new upstream GPU-demo AUTO setting
requires both the GPU and utility targets, which are OFF here; no demo or GPU
backend is built.

## Compilation and validation

**MEASURED locally:** Apple M4 arm64, macOS 26.5, Apple Clang 21.0.0, Release
(`-O3 -DNDEBUG`), repository-pinned dependencies. Reconfigured and rebuilt the
complete existing GUI.Forms build with prepared-text and text-mask profiles ON.
All 98/98 CTest tests pass. A separately linked `hb_version_string()` probe
reports `14.6.0` from the rebuilt static library.

**MEASURED Windows cross-compilation:** x86_64-w64-mingw32 GCC 16.1.0, Release,
GUI.Forms' own CMake HarfBuzz/FreeType profile. Both the old 14.2.1 control and
new 14.6.0 build complete successfully; both emit the same warning. This is a
Windows-target compilation result, not a native Windows runtime test. The old
native Windows CI used GCC 16.2.0 and emitted the same diagnostic.

The new HarfBuzz CMake build respects the parent's C++20 setting and compiles
separate translation units instead of its former amalgamation. The diagnostic
now occurs while compiling `hb-ot-font.cc`, rather than `harfbuzz.cc`.

Local evidence is preserved under `.build/harfbuzz-update/evidence/`:
`mingw-old-build.log`, `mingw-new-build.log`, `macos-build.log`,
`macos-tests.log`, and the configure/fetch logs. The native build is
`.build/native-live/gui-forms`; the cross build is
`.build/harfbuzz-update/mingw`.

## Remaining warning and source analysis

The diagnostic concerns **memcmp**, not memcpy:

```text
src/hb-algs.hh:1247:17: warning: 'int memcmp(const void*, const void*, size_t)'
specified bound [2147483648, 4294967295] exceeds source size 1850
[-Wstringop-overread]
```

**OBSERVED source path:** `hb_ot_get_glyph_from_name` calls the OpenType `post`
table accelerator, whose binary-search comparator obtains a glyph-name byte
span and calls `hb_array_t::cmp`. That method returns on unequal lengths, then
calls `hb_memcmp` for equal lengths. `hb_memcmp` handles zero length and delegates
to the C library `memcmp`.

For built-in names, `hb-string-array.hh` builds a constant string pool and offset
table from `hb-ot-post-macroman.hh`. A span length is calculated as
`offset[i + 1] - offset[i] - 1`, using unsigned arithmetic. Inspection of all
258 entries gives a pool of exactly 1,850 bytes and a maximum name length of
16 bytes (`nonmarkingreturn`). The `post` accelerator checks the glyph/index
against the table's entry count before obtaining the span. Font-supplied custom
names instead use a one-byte length and a separately bounded pool.

**INFERENCE from the diagnostic and source guards:** GCC's optimized, inlined
range analysis does not retain a sufficiently tight bound for the unsigned
span-length calculation and comparison. Its reported 2–4 GiB range is
inconsistent with the inspected built-in table and equal-length comparison
path, so this specific diagnostic is consistent with a compiler false positive.
This is a source-based assessment, not an upstream-confirmed compiler bug.
The relevant comparator and string-table generator are unchanged between
14.2.1 and 14.6.0; upgrading therefore does not remove the warning.

No warning suppression, vendored source patch, or compiler-option workaround
was applied. Apple's Clang build emits no HarfBuzz memory-operation warning.
HarfBuzz still emits its separate CMake configuration notice because upstream
primarily supports Meson and labels CMake support community-maintained. The
Mac build also retains pre-existing toolkit deprecation/linker warnings; this
intake does not claim a globally warning-free build.
