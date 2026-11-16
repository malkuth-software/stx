# stx_map

A small built-in world map and its decoder: Natural Earth 1:50m coasts,
lakes and admin-0 borders, baked into a 47,937-byte TMAP 2 file that is
compiled into the library, and a C++ decoder that turns it (or any TMAP 2
file a caller supplies) into integer polylines. No renderer: a consumer
projects and draws the lines itself. No dependencies.

## The map

| Layer | Kind | Lines | Vertices | Loaded |
|---|---|---|---|---|
| `land` | rings | 1,399 | 27,999 | by default |
| `lakes` | rings | 463 | 6,565 | by default |
| `borders_admin0_ne_default` | lines | 391 | 7,207 | only when asked for by name |

Positions are integers, 32,768 units per degree (`units_per_degree`), x
from 180 degrees west, y from 90 degrees south, growing northward. Every
decoded line lies within 744 units (0.0227 degrees, about 2.5 km) of its
Natural Earth source line, the symmetric Hausdorff distance; the file
states that tolerance (`MapInfo::tolerance_units`) and the bake measured
0.02262 degrees.

The border layer is **Natural Earth's default (de facto) boundaries**: its
admin-0 boundary lines as published by default, following de facto control,
taken exactly as fetched. It is kept apart from the other layers and loaded
only on request, so a consumer that draws no borders never decodes them.
A border end that met a coast or another border in the source is tied to
it, so it meets it exactly when decoded. PROVENANCE.md records the source
release, its hashes and this choice.

## Use

```cpp
#include <stx_map/map.hpp>

const std::span<const std::byte> file = stx_map::embedded_world_map();
stx_map::MapInfo info;
if (stx_map::inspect_map(file, stx_map::DecodeLimits{}, info) != stx_map::Status::ok) { /* refused */ }
// One LayerStorage per layer, sized from info.layers[i].vertices and
// .lines + 1, for each layer to be decoded.
const stx_map::LayerMask mask = stx_map::with_dependencies(info,
    stx_map::default_layers(info) | stx_map::layer_named(info, "borders_admin0_ne_default"));
const stx_map::Status status = stx_map::decode_map(file, info, stx_map::DecodeLimits{}, mask, storage);
const stx_map::LayerView land = stx_map::view_of(info.layers[0], storage[0]);
```

- `map.hpp`: `inspect_map` checks a whole buffer (CRC-32, header, every
  count) before anything is decoded; `decode_map` fills caller storage with
  integer arithmetic only and allocates nothing. Refusals are a `Status`.
  `embedded_world_map()` is the built-in file; any caller buffer holding a
  TMAP 2 file decodes the same way.
- `levels.hpp`: `simplify_layer` derives a coarser level at load, exact
  integer Douglas-Peucker into caller storage (ends kept, so tied borders
  still meet their coasts).
- `index.hpp`: a uniform grid over line bounding boxes for viewport culling
  (`build_index`, `query_index`), in caller storage.
- `projection.hpp`: equirectangular and Web Mercator, to 0..1 world
  coordinates with y from the north.

`docs/FORMAT.md` describes TMAP 2. `tools/bake/tmap_decode.js` is the
reference decoder (standalone JavaScript, usable in a page); the C++
decoder gives the same positions, which the tests check.

## Tests

`tests/map.cpp`: the embedded file against the reference decoder's output
(`tests/data/reference.bin`), every eighth stored line against its source
within the tolerance (`tests/data/check_lines.bin`, written by the bake),
layer selection and dependencies, refusals of damaged buffers, levels, the
index, the projections, and decode timing printed as a workload.
`fuzz/map_decode_fuzz.cpp` is a libFuzzer target (its mutator repairs the
CRC) and, standalone, a deterministic damage sweep. `tests/installed` is
an installed-consumer fixture. `node tools/bake/test_codec.js` round-trips a
synthetic map through the writer and the reference reader.

## Rebaking

The bake is deterministic: the same inputs give the same file
(`data/bake.txt` is the shipped bake's log).

```text
# The four Natural Earth v5.1.2 files (PROVENANCE.md) in <ne_dir>, from
# https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/<file>
python3 -I tools/bake/load_natural_earth.py <ne_dir> <work>/source.json
node tools/bake/bake.js <work>/source.json <work>/out
cp <work>/out/world_map.tmap data/
cp <work>/out/check_lines.bin tests/data/
cp <work>/out/bake.txt data/
node tools/bake/embed_map.js           # src/world_map_data.inc
node tools/bake/make_test_data.js      # tests/data/reference.bin
node tools/bake/make_heading_table.js  # src/heading_table.inc (fixed)
node tools/bake/test_codec.js
```

`bake.js` optimises every line's turtle chain on four worker threads over
seven rounds of cost-table refinement, ties line ends that met in the
source, writes the file, decodes it again with the reference decoder and
fails unless every line lies within the tolerance and matches its plan.
The shipped bake: 47,937 bytes (starts 4,531, steps 41,385, ties 1,869),
error 0.02262 degrees; 732 line ends met another line in the source, 487
were tied (144 onto coasts, 340 onto another border's end, 3 onto a border
vertex) and 43 ties were dropped because they would have passed the
tolerance; the other 202 are each the first end of a group of meeting
ends, which the rest of its group is tied to.

stx_map is MIT licensed (LICENSE). The map data is Natural Earth's, in the
public domain.
