# Mutable transform shader: propagate child failure

The mutable coordinate wrapper used by atlas and vertex draws previously ignored
its child's `appendStages` result and always reported success. An empty child
therefore left only coordinate stages while the caller believed it had a usable
color-producing shader. The wrapper now returns the child's result so the caller
can discard the failed pipeline.

A deterministic test using an empty child failed with exit code 5 before the fix.
After the fix, the candidate passes 56/56 release tests. The reference preserves
the original success result explicitly and passes 55/55. All thirteen common
output files remain byte-identical. The full instrumented candidate suite returns
PASS; diagnostic contents were not inspected. The regression is kept separate
from the subsequent transform/image-shader conversion.

Original transform header and source are retained under baseline/skia, and the
reference CMake list selects that source. Existing shader blend tests cover
successful vertex drawing; the new regression covers the previously masked
failure path. This fix does not promise rollback of stages already appended:
callers discard a shader pipeline when construction returns false.
