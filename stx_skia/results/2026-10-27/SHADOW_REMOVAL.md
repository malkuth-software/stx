# Shadow drawing, polygon utilities and priority queue: removal — 2026-10-27

## Use check

| Facility | Users before this change | Outcome |
|---|---|---|
| `SkShadowUtils` (public, installed `include/utils/SkShadowUtils.h`) | None in the library, GUI.Forms at its pin or on `main`, or the library's tests | Removed |
| `SkCanvas::private_draw_shadow_rec` / `onDrawShadowRec` | `SkShadowUtils::DrawShadow` only | Removed |
| `SkDevice::drawShadow` (defined in `SkShadowUtils.cpp`) | `SkCanvas::onDrawShadowRec` only | Removed |
| `SkShadowTessellator`, `SkDrawShadowInfo` | `SkShadowUtils` only | Removed |
| `SkPolyUtils` | `SkShadowTessellator` only. Two library tests called it, but only as a consumer for the containers they test | Removed |
| `SkTDPQueue` | `SkPolyUtils` only, plus its own test | Removed |
| `SkTInternalLList` | `SkPolyUtils` and `SkDiscardableMemoryPool` | Kept, since the pool uses it |

No differential corpus drew shadows, and every shared artifact is unchanged.

## Change

- **Moved unchanged:** `SkShadowUtils.h/.cpp`, `SkShadowTessellator.cpp/.h`, `SkPolyUtils.h` and
  `SkDrawShadowInfo.cpp/.h` had not changed since intake. They move unchanged, with their
  notices, to `baseline/skia`.
- **Deleted:** the candidate's converted `SkPolyUtils.cpp` and `SkTDPQueue.h`. Their intake
  originals were already in `baseline/skia`.
- **Reference build:** it compiles the three baseline shadow sources.
- **Declarations removed:**
  - `SkCanvas` loses the shadow entry point, its virtual and the `SkDrawShadowRec` forward
    declaration;
  - `SkDevice` loses `drawShadow`;
  - the virtual enforcer and the no-draw canvas lose their overrides.
- **Tests:**
  - `tests/priority_queue.cpp` is removed with the queue, along with its three measurement and
    comparison tools. Their recorded results stay in `results/`.
  - `tests/intrusive_list.cpp` drops its triangulation case and keeps the discardable-pool
    case, the list's other consumer.
  - The upstream `ShadowTest.cpp`, `PolyUtilsTest.cpp` and `TDPQueueTest.cpp` stay in
    `skia/tests`, unbuilt.
- **Absence checks:**
  - `tests/canvas_device.cpp` fails to compile if any of the five headers returns to the
    candidate.
  - An expression-based concept confirms the canvas has no shadow entry point in the
    candidate, and has one in the reference.
  - The installed-consumer test checks that `SkShadowUtils.h` is not installed.

## Verification

- **Builds:**
  - candidate: 87/87 (one test fewer);
  - reference: 79/79;
  - instrumented suite: PASS.
- **Artifacts:** every shared artifact is identical, with the five intentional differences as
  before.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **Inventory:** the AST inventory falls from 1,502 excluded constructs in 76 files to 1,055
  in 70.
