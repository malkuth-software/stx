# Uncompiled upstream directories: removal — 2026-10-27

## What moved

These eleven `skia/src` directories, 894 files in all, move to `baseline/skia` unchanged:

| Directory | Files |
|---|---|
| `gpu` | 647 |
| `codec` | 94 |
| `pathops` | 58 |
| `pdf` | 47 |
| `encode` | 22 |
| `partition_alloc` | 8 |
| `xml` | 7 |
| `xps` | 4 |
| `svg` | 4 |
| `android` | 3 |
| `capture` | 1 |

So do 58 headers outside them that only they included:
- 21 in `include/gpu`, 18 in `include/codec` and 2 in `include/encode`;
- 9 codec metadata headers in `include/private`, including its Chromium subdirectory;
- `include/svg/SkSVGCanvas.h` and `include/android/SkAnimatedImage.h`;
- five headers in `src/core`: `SkArenaAllocList.h`, `SkBitmaskEnum.h`, `SkIPoint16.h`,
  `SkRandom.h` and `SkScopeExit.h`.

Every moved file was unchanged since intake. The candidate's edited copy of
`src/image/SkImageGeneratorPriv.h` is deleted, because its intake original was already in
`baseline/skia`. The candidate tree's `skia/src` and `skia/include` now hold 795 tracked files.

## How "dead" was decided

- **Sources:** the candidate's `compile_commands.json` lists no source in the eleven
  directories.
- **Headers:** the candidate's Ninja dependency records (`ninja -t deps`), covering the library
  and every test, read no header in them.
- **Headers elsewhere:** a header outside the directories moves only if the candidate never
  reads it, it has at least one includer in `skia/src` or `skia/include`, and every includer is
  in the eleven directories or is itself moving. This was computed to a fixed point.
  - Unbuilt upstream trees, such as `skia/tests`, `gm` and `tools`, were not counted as
    includers.
  - Headers the candidate does read stay, for example `include/gpu/GpuTypes.h`,
    `include/gpu/ganesh/GrTypes.h` and `include/codec/SkEncodedOrigin.h`.

## The reference build

The reference compiled 25 sources from `codec`, `encode` and `android` in the candidate tree,
and read 43 headers from `codec`, `encode`, `partition_alloc` and `android`.
- **Sources:** `cmake/reference.cmake` now rewrites every source path in the eleven directories
  to `baseline/skia`.
- **Headers:** they resolve there because the reference searches `baseline/skia` first.
- **Check:** after the move, it compiles all 20 codec sources from `baseline/skia` and none from
  the candidate tree.

## Proof

- **Mirror:** `m4build` removed the moved files from the build mirror; none of the directories
  has a file there.
- **Gates:**
  - candidate: 88/88;
  - reference: 80/80;
  - instrumented suite: PASS;
  - shared artifacts: unchanged, with the six intentional differences as before.
- **Fresh build:** a candidate build in a new directory (`build/fresh-candidate`) compiles all
  379 targets and passes 88/88.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** passes 9/9 on macOS with pinned LLVM 22.1.8 (`tools/verify_gui.sh`).
- **Absence check:** `tests/canvas_device.cpp` probes one header from each removed area.
- **Linux and Windows:** fresh builds of both profiles run in CI on PR #1.
- **Inventory:** unchanged at 801 constructs in 58 files, since none of the moved files was
  compiled.
