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
| `unicode/` | Unicode 17.0.0 grapheme data: licence and provenance of GUI.Forms' generated tables | per-file SHA-256 | [unicode/README.md](unicode/README.md) |
| `fonts/noto/` | Noto Sans CJK JP and Noto Emoji, GUI.Forms' showcase fallback fonts | per-file SHA-256 | [NOTO_FALLBACK_FONTS_INTAKE.md](NOTO_FALLBACK_FONTS_INTAKE.md) |

`unicode/` is the subset GUI.Forms vendored, moved unchanged from its
`third_party/` on 2026-11-05 (every blob equal to GUI.Forms' at `7f5d4648`);
its README, copied unchanged, describes the original intake (paths in it are
GUI.Forms'). libunibreak 8.0, MD4C 0.6.0, SheenBidi 2.9.0 and HarfBuzz 14.6.0
were removed on 2026-11-16: GUI.Forms reads Markdown with stxlib::markdown,
takes bidi levels, line breaks and script runs from stxlib::textlayout and
shapes with STXlib's shaper (STXlib, pinned in `stxlib.lock`); they are in
this repository's history before that.

The fonts are the exact files GUI.Forms' `fetch_showcase_fallback_fonts.sh` pinned by SHA-256.

To check a git-pinned copy, compare every blob under the directory with the
pinned commit's blob at the same path and mode (`git ls-tree -r`); the intake
note names the only paths that may be missing. A file pinned by SHA-256 is
checked against its recorded digest.
