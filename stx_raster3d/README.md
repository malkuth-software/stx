# stx_raster3d

A deterministic CPU renderer for 3D plots: CONV surfaces tessellated
adaptively, grids and caller triangles, polylines and markers, drawn into a
supersampled visibility buffer and shaded once per primitive and pixel.
Every frame can be picked exactly.

- `memory.hpp`: the caller's allocator and the buffer every owner uses.
- `executor.hpp`: the caller's executor (tasks are bands of rows).
- `camera.hpp`: perspective (matplotlib's focal length) and orthographic
  cameras, the matplotlib orbit (elevation, azimuth, roll), and the view.
- `mesh.hpp`: meshes, the adaptive tessellation of a `stx_conv::surface`,
  grids (smooth or flat, holes where a corner is not finite).
- `raster.hpp`: materials (colour maps, two-sided lighting, iso-lines),
  lines, markers and the renderer; picture as premultiplied BGRA8.
- `picking.hpp`: Newton refinement of a surface pick onto the surface.

Determinism: binary64 geometry, fixed-point coverage, binary32 depth keys,
no fused multiply-add (`-ffp-contract=off`), fixed primitive order and one
task per band, so the picture is the same bytes on one thread or many.

Depends on `stx_conv`. Build alone with `cmake -S stx_raster3d -B build`
(finding an installed `stx_conv`), or from the stx root.
