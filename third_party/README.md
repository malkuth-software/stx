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

To check a copy, compare every blob under the directory with the pinned
commit's blob at the same path and mode (`git ls-tree -r`); the intake note
names the only paths that may be missing.
