# MD4C

MD4C 0.6.0, commit `7fc1815a5eeba2af7d6120a76202bf59f3b6e6e4`, from
<https://github.com/mity/md4c>, MIT (LICENSE.md). Files: `md4c.c`, `md4c.h`,
`entity.c`, `entity.h` from upstream `src/`, and `LICENSE.md`; all unmodified.
Copied 2026-10-28 from SwiftEdit's unmodified vendored copy; each file's Git
blob identity was checked against the upstream tree at that commit (see
`../MD4C_INTAKE_2026-10-28.md`). GUI.Forms builds the parser and the entity
table only, not the HTML renderer or the command-line tool. Upstream code is
outside the first-party house style audit; the adapter in
`src/core/text/markdown/markdown.cpp` is inside it.
