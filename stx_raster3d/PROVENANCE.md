# stx_raster3d provenance

stx_raster3d is a house-style rewrite of the CPU renderer of conv-teapot
(malkuth-software/conv-teapot at fc0a609, MIT, the owner's), keeping its
algorithms:

- `src/raster.hpp`: the visibility-buffer rasteriser (24.8 fixed-point edge
  functions, the top-left fill rule, binning by bands of rows,
  perspective-correct weights from screen-affine ones, the nearest primitive
  by depth key),
  and the overlay bias that lets lines win against the surface they lie on.
- `src/render.hpp`: the tree tessellation of a CONV surface (halving by
  edge-midpoint and centre error against a tolerance, an optional normal
  angle limit, crack-free fans over every border vertex, the shorter
  diagonal for plain quads, the periodic closing column and pole normal
  averaging), shading once per primitive and pixel, iso-lines, and the
  seeded Newton solve for exact picking.

What changed: storage comes from a caller-supplied allocator, work from a
caller-supplied executor (one task per band of rows), nothing throws, every
failure leaves the previous frame or mesh as it was, and the integer
parameter lattice is 64 units per cell (power-of-two halving) instead of
960. Clipping to the near plane and the picture (conv-teapot left out a
triangle with a corner behind the near distance), lines, markers, orthographic cameras, colour maps, grids taken as they
stand and value iso-levels are new.

Not taken: conv-teapot's physics (zc_phys), wrench.js, its teapot data
(`teapot_patches.inc`, `utah_*.inc`) and everything under `gpu/`. Its
captures depend on the Utah data, so they are not this package's
regression oracle; the tests here state their own expectations.

The CONV surfaces themselves are `stx_conv::surface` (this repository).
