# Third-party sources

Upstream libraries kept in stx so that GUI.Forms and the other stx consumers
build them from a pinned copy without downloading anything. GUI.Forms' main
repository carries no third-party sources; it reaches these through its
stx.lock (owner, 2026-11-05).

Each directory is the upstream tree at its pinned commit, byte for byte, except
for the paths its intake note lists as left out. No file in it is edited; a
local build setting (an options header, a CMake switch) belongs to the
consumer. `.gitattributes` marks `third_party/**` `-text`, so git never
converts a line ending and every blob stays the pin's. Each intake note records
the upstream, the pinned commit and tree, what was left out, how the copy was
checked, and the licence.

| Directory | Version | Pinned commit | Intake |
|---|---|---|---|
| `freetype/` | FreeType 2.14.2 | `f4205da14867c5387cd6a329b90ee10a6df6eeff` | [FREETYPE_INTAKE.md](FREETYPE_INTAKE.md) |
| `harfbuzz/` | HarfBuzz 14.6.0 | `c7a7457b7385f33178e8cf87615ca077a810bbe7` | [HARFBUZZ_INTAKE.md](HARFBUZZ_INTAKE.md) |
| `libunibreak/` | libunibreak 8.0 | `28a2756b864c343f438cd22537d49d394d4666a5` | [LIBUNIBREAK_INTAKE_2026-10-01.md](LIBUNIBREAK_INTAKE_2026-10-01.md) |
| `md4c/` | MD4C 0.6.0 (parser and entity table) | `7fc1815a5eeba2af7d6120a76202bf59f3b6e6e4` | [md4c/PROVENANCE.md](md4c/PROVENANCE.md), [MD4C_INTAKE_2026-10-28.md](MD4C_INTAKE_2026-10-28.md) |
| `sheenbidi/` | SheenBidi 2.9.0 (Headers and Source) | `829d99918adf05761e9f35611dc13e76e6810f80` | [sheenbidi/PROVENANCE.md](sheenbidi/PROVENANCE.md) |
| `unicode/` | Unicode 17.0.0 grapheme data: licence and provenance of GUI.Forms' generated tables | per-file SHA-256 | [unicode/README.md](unicode/README.md) |
| `fonts/noto/` | Noto Sans CJK JP and Noto Emoji, GUI.Forms' showcase fallback fonts | per-file SHA-256 | [NOTO_FALLBACK_FONTS_INTAKE.md](NOTO_FALLBACK_FONTS_INTAKE.md) |

libunibreak is the upstream tree at its pin, whole. `md4c/`, `sheenbidi/` and
`unicode/` are the subsets GUI.Forms vendored, moved unchanged from its
`third_party/` on 2026-11-05 (every blob equal to GUI.Forms' at `7f5d4648`);
their PROVENANCE files and the two dated intake records, copied unchanged,
describe the original intake (paths in them are GUI.Forms'). The fonts are the
exact files GUI.Forms' `fetch_showcase_fallback_fonts.sh` pinned by SHA-256.

To check a git-pinned copy, compare every blob under the directory with the
pinned commit's blob at the same path and mode (`git ls-tree -r`); the intake
note names the only paths that may be missing. A file pinned by SHA-256 is
checked against its recorded digest.
