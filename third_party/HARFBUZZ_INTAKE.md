# HarfBuzz 14.6.0 intake

- Upstream: https://github.com/harfbuzz/harfbuzz.
- Pinned commit: `c7a7457b7385f33178e8cf87615ca077a810bbe7` (release 14.6.0),
  tree `24385871a6faf49b2a5352989caef49fff08b0d8`, the pin of GUI.Forms'
  `third_party/fetch_text_stack.sh` when it moved here on 2026-11-05. Why this
  release was taken, and the compiler warning it was checked for, are in
  [HARFBUZZ_14_6_INTAKE_2026-10-06.md](HARFBUZZ_14_6_INTAKE_2026-10-06.md),
  GUI.Forms' original intake record, copied here unchanged.
- Kept: 768 files, every path of the pinned tree outside `test/` and `perf/`.
- Left out (owner, 2026-11-05): `test/` (upstream's test suites and their
  fonts, 92 MB) and `perf/` (benchmarks and their fonts, 6 MB), 3,105 files in
  all. Neither is part of the library build. This is the cut GUI.Forms' SDK
  already shipped (`tools/package_sdk.py`).
- `CLAUDE.md` is upstream's symbolic link to `AGENTS.md`, kept as a link
  (a Windows checkout without symbolic links shows it as a file holding the
  link text).

## How the copy was made and checked

No download. The source was the checkout `fetch_text_stack.sh` had already
fetched into a GUI.Forms workspace (`D:/gui_forms_int/gui_forms/third_party/harfbuzz`),
clean and detached at the pin. `git archive` of the pinned commit, with
`core.autocrlf=false` so no line ending was converted, wrote the tree;
`test/` and `perf/` were removed, and the executable bits and the `CLAUDE.md`
link were restored in the index. Every blob stx records under
`third_party/harfbuzz/` was then compared with the pinned commit's blob at the
same path and mode: 768 equal, none different or extra, and every missing path
is under `test/` or `perf/`. Because git names a blob by its content, equal
blob ids mean byte-identical files.

## Licence

HarfBuzz is under the "Old MIT" licence in `COPYING`, which also lists its
copyright holders. A few files carry other permissive notices of their own
(named in `COPYING`); they stay with those files. GUI.Forms installs `COPYING`
with its SDK.
