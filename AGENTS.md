# stx implementation guardrails

This repository holds the stx libraries (README.md). Each package directory
is self-contained: it may not include, link or depend on another repository's
sources, and depends on another stx package only through that package's
installed CMake config (`stx_png` on `stx_zlib`). No package includes a
GUI.Forms header.

## Code style (mandatory)

All first-party C++ follows the programming house style, kept in
`falseywinchnet/EpistemicGPT` as `programming-house-style.md` (GUI.Forms
carries a verbatim copy as `planning/PROGRAMMING_HOUSE_STYLE.md`). Read it in
full before writing or changing code. Before committing, run the checkers
from the pinned template_harness over everything you touched:

```text
python3 template_harness/harness/lock.py --lock harness.lock checkout template_harness
python3 -B template_harness/harness/check_house_style.py stx_zlib stx_png stx_conv stx_rtf2md stx_vorbis
python3 -B template_harness/harness/check_lifetime_rules.py stx_conv stx_skia
python3 -B tools/check_stx_skia_style.py
```

`stx_skia/skia/` holds files imported from Skia that are still being
converted; `tools/check_stx_skia_style.py` checks every C++ file in
`stx_skia/` against the shrink-only allowance `stx_skia/house_style_allowance.txt`
(`stx_skia/AGENTS.md`).

## Boundaries

- C++20; C only where a package already has it (`stx_vorbis`).
- `stx_skia` is CPU only: no Ganesh, Graphite, GL, Vulkan, Metal, Direct3D,
  Dawn or WebGPU is compiled, linked or probed.
- No GPU, no platform UI, no network access at build or run time.
- A package's public headers carry no third-party types.
- Each package keeps its LICENSE (and PROVENANCE.md, where it has one) current;
  a change derived from other work names it there.
- Memory errors and undefined behaviour are defects; the sanitizer jobs in
  `.github/workflows/` (dispatch) and the local sanitizer runs must stay clean.

## Workflow

Workers commit on local branches and never push; the coordinator folds and
lands them (template_harness `docs/workflow.md`).
