# Unicode grapheme data provenance

Status: **OBSERVED** source data and **MEASURED** conformance input. This is
not a runtime dependency and does not select a shaping backend.

GUI.Forms pins Unicode 17.0.0 data for extended grapheme segmentation. The
normal build consumes checked-in generated C++ headers and performs no network
access. `tools/unicode/generate_grapheme_tables.py` regenerates those headers
from explicitly supplied source paths after verifying every SHA-256 digest.

| Input | Official source | SHA-256 |
| --- | --- | --- |
| Grapheme break properties | <https://www.unicode.org/Public/17.0.0/ucd/auxiliary/GraphemeBreakProperty.txt> | `d6b51d1d2ae5c33b451b7ed994b48f1f4dc62b2272a5831e7fd418514a6bae89` |
| Indic conjunct break properties | <https://www.unicode.org/Public/17.0.0/ucd/DerivedCoreProperties.txt> | `24c7fed1195c482faaefd5c1e7eb821c5ee1fb6de07ecdbaa64b56a99da22c08` |
| Extended pictographic properties | <https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt> | `2cb2bb9455cda83e8481541ecf5b6dfda66a3bb89efa3fa7c5297eccf607b72b` |
| Grapheme conformance cases | <https://www.unicode.org/Public/17.0.0/ucd/auxiliary/GraphemeBreakTest.txt> | `e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec` |

Generated outputs:

- `src/core/unicode_grapheme_data.hpp` — production property ranges.
- `tests/unicode_grapheme_break_cases.hpp` — 766 conformance cases with
  source-line provenance.

The source data and generated derivative tables are covered by the
[Unicode License v3](LICENSE.txt). Unicode is a registered trademark of
Unicode, Inc.
