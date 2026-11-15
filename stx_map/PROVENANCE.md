# stx_map provenance

## The map data

`data/world_map.tmap` (and its embedded copy, `src/world_map_data.inc`) is
baked from Natural Earth 1:50m vector data, which is in the public domain
(https://www.naturalearthdata.com/about/terms-of-use/). The input is the
release tag v5.1.2 of Natural Earth's GitHub repository,
nvkelso/natural-earth-vector, commit
f1890d9f152c896d250a77557a5751a93d494776, files under `geojson/`:

| File | SHA-256 | Git blob |
|---|---|---|
| ne_50m_land.geojson | e874b27a51d146452be360cafb3cc50c86001074a67d534113e6534682f9826b | c412c52b5286ba727dcb7047ecd6080bcbeb8298 |
| ne_50m_lakes.geojson | d350b75978b26fe839b797c2c529b2fb8f47fb3983c03f4964e36d5df9378a52 | 5ddced58279ea13ae5023929f135bb7ecfd12d95 |
| ne_50m_admin_0_boundary_lines_land.geojson | 2faac4f6b34386f3d21b6e018cf151f241f00e5c936d44dd17d7d9bfb147fa48 | f2da10de881015c7182951b59a75b1e4e27356c6 |
| ne_50m_ocean.geojson (checked, not used) | 5b810162af4b20805838cfd766ef1a445c350cf6aafc8b589515daef30033adf | 09ec7f4c65735e46badd82d42f3a0fdcdb7d8c33 |

The git blob hashes are the ones at that tag (the same blobs are at v5.1.0
and v5.1.1). `tools/bake/load_natural_earth.py` refuses any file whose
SHA-256 differs.

Layers:

- `land`: ne_50m_land, rings.
- `lakes`: ne_50m_lakes, rings.
- `borders_admin0_ne_default`: ne_50m_admin_0_boundary_lines_land, lines.
  These are **Natural Earth's default (de facto) boundaries**: the admin-0
  boundary lines as Natural Earth publishes them by default, which follow
  de facto control, taken exactly as fetched. Natural Earth also publishes
  boundaries from other points of view; stx_map does not choose between
  them. The layer is optional: a decoder loads it only when asked for it by
  name. This choice is the owner's (2026-11-15).

Before coding, rings smaller than 0.04 degrees across and lines that keep
too few points after a 0.02-degree simplification are left out (the
selection of the experiment below); every other line is coded within the
stated tolerance of its source.

## The codec

The TMAP format, the turtle-chain optimiser and the range coder
(`tools/bake/turtle.js`, `turtle_dp.js`, `model.js`, `rc.js`, `check.js`)
are taken unchanged from the owner's map codec experiment (the projects
repository, branch claude/airspy-map-codec at b796df6,
`demos/airspy_map/map_codec/`, whose README records the format's design,
results and findings). New here: the TMAP 2 container (layer records with
kinds, names, the optional flag and dependencies, the stated tolerance and
unit, the CRC-32), ties between line ends that met in the source, the
loader of the pinned files (a port of the experiment's bake helpers to the
pinned release), the reference decoder `tools/bake/tmap_decode.js` and the
C++ library.

stx_map is MIT licensed (LICENSE); the map data stays in the public domain.
