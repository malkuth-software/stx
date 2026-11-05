# GUI.Forms integration of the red/blue swap kernel — 2026-10-27

`integration/gui_forms_8d1f2be0_swizzle.patch` replaces GUI.Forms' four RGBA/BGRA byte loops
with `stx_skia::swap_red_blue` and `swap_red_blue_rows`. It applies on top of the existing
renderer patch (`integration/gui_forms_8d1f2be0.patch`, unchanged byte for byte) in the
pinned fixture (GUI.Forms 8d1f2be0).
- **How it is made:** `tools/prepare_gui_fixture.py` generates both patches from the pinned
  sources as two rewrite stages. `--patches-only` regenerates them without preparing a
  fixture.
- **What was not touched:** the authoritative GUI.Forms checkout. `origin/main` was fetched
  for reading only, and its branch and local files are as they were.

## Changes

| File | Before | After |
|---|---|---|
| `src/controls/raster_canvas/raster_canvas.cpp` | `rgba_to_bgra` allocated a new vector per tile (publication) and per damaged patch (synchronization), and swapped byte by byte | `swap_red_blue_rows` into one buffer reused across all tiles or patches of a call. On failure the canvas reports `invalid_image_data` and returns false; publication also releases its tiles. Image loads and patches copy the rows they are given, so refilling the buffer is safe |
| `src/render/coregraphics/raster/coregraphics_raster.cpp` | byte loop from a BGRA resource into RGBA rows | `swap_red_blue`; a rejected swap leaves the image unsynchronized, as other failures there do |
| `src/abi/drawing_platform_windows.cpp` | `copy_as_bgra` copied, then swapped in place in a second pass | One disjoint `swap_red_blue` for RGBA snapshots, a plain copy for BGRA. It returns `bool`. `present_surface` throws on failure before acquiring a device context; `acquire_hdc` releases its DIB and DC, then throws |
| `src/host/windows/application/windows_host.cpp` | byte loop into `live_bgra_scratch_` | `swap_red_blue` into the same reusable scratch, so the DIB lifecycle test's no-allocation check for BGRA still holds. A rejected swap returns before any DC change |
| `tests/canvas_tests.cpp` | — | New `rgba_tiles_match_bgra_tiles` (below) |
| `CMakeLists.txt` | — | Links `stx_skia::swizzle` privately into the seven targets that compile these files, each guarded by `if(TARGET ...)`: `gui_forms_controls`, `gui_forms_coregraphics`, `gui_drawing_c_api`, `gui_forms_host_windows`, and the Windows DIB-lifecycle, tooltip and prepared-text tests |

All three files that `stx_skia_swizzle` adds to a host include only the standard library. The
kernel links no Skia code.

**Export.** `gui_forms_controls` is in the `GUIFormsTargets` export, so its private link
becomes a link-only reference to `stx_skia::swizzle`. That target is in stx_skia's own
export. Configure and generate accept it.

## The new test

`rgba_tiles_match_bgra_tiles` checks that both bitmap formats produce the same resources.
- **Setup:** it builds the same picture in a 1100×600 RGBA bitmap and a BGRA bitmap, using
  opaque random colours on a sparse grid.
- **Publication:** it requires all six tile resources of the two canvases to be identical
  byte for byte, which covers gutters and edge tiles.
- **Edits:** it then makes four edits in both bitmaps, synchronizes after each, and requires
  identical resources again:
  - a 30×230 block crossing both tile boundaries;
  - a one-pixel column on a boundary;
  - a full-width one-pixel row;
  - a block in the bottom-right edge tile.

Edits reach the canvas as strided patches, so this covers `swap_red_blue_rows` with source
strides wider than the patch.

## Verification (M4, pinned LLVM 22.1.8, `tools/verify_gui.sh`)

`verify_gui.sh` now also builds and runs `gui_forms_canvas_tests`, which reaches the
canvas code, and `gui_forms_coregraphics_smoke`. The smoke test loads 1×1 and 2×1 BGRA
resources through the CoreGraphics swap and checks drawn pixel values.

1. **Baseline:** the original four loops plus the new test passed 9/9
   (`build/swizzle-gui-baseline.log` on the mirror). The new test therefore holds for
   GUI.Forms' own loops.
2. **Patched:** the full patch passed 9/9 (`build/swizzle-gui.log`). Both converted files
   recompiled without warnings. The only CMake warning (HarfBuzz) appears in both runs.

The four-file backup taken before applying the patch, with the untouched test file, is at
`build/gui-fixture-pre-swizzle/`, locally and on the mirror. The patch was applied to the
local fixture and copied to the mirror's, and the two were checked identical by SHA-256.

The swap rewrites also apply unchanged to GUI.Forms `main` (62d8499d), so the patch should
survive re-pinning the fixture.

## Windows (CI)

`.github/workflows/gui-windows.yml` covers Windows, at 5cbfe623 in run 40583645977.
- **Toolchain:** it restores GUI.Forms' pinned MSYS2 CLANG64 toolchain (clang 22.1.8) with
  GUI.Forms' own tool.
- **Fixture:** it prepares the fixture and checks that both patches regenerate byte for byte.
- **Configuration:** it configures as GUI.Forms' Windows CI does (Windows host on, Skia adapter
  off, HarfBuzz text on), with transactional DIB tests enabled.
- **Builds:** both Windows files compile, with every target that includes `windows_host.cpp`.
  Six tests include it textually, and the first run failed because they were not yet linked to
  the kernel (fixed in be5f59a3).

**All ten tests pass:**
- live-surface endpoint;
- DIB lifecycle, which converts RGBA and BGRA live surfaces;
- tooltip;
- the cross-platform canvas tests, including `rgba_tiles_match_bgra_tiles`;
- control snapshot;
- canvas raster;
- damage rectangles;
- text;
- part composite;
- content pixels.

**The drawing probe** prints `drawing-windows-rgba=pass` and `drawing-windows-hbitmap=pass`. Its
new RGBA case reads the expected GDI pixels through a leased HDC and from a presented memory DC.

**GUI.Forms bug confirmed.** The probe also prints `drawing-windows-rgba-lease-roundtrip=ff0000ff`.
A red RGBA pixel (`0xffff0000`) comes back blue after an HDC lease, because `release_hdc` copies
the BGRA lease back into an RGBA bitmap without swapping. This is outside the four converted
loops and is not fixed by the patch.

## Not verified
- **Not converted:** a fifth copy of the byte loop, the BGRA-to-RGBA conversion in
  `src/core/engine/primitives/material/material_image.cpp:152`. It was not in the requested
  four.
- **Live-surface format:** changing the native live-surface format to BGRA everywhere, which
  would remove the Windows live-surface conversion, remains a separate GUI.Forms decision.
