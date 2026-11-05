# TArray and private-header security review

The Codex Security Standard scan `868f3e52-d621-40f1-a688-0b39a3060c5a`
sealed a source review of `skia/include/private` at commit `92e41ac`. An
independent baseline and three focused investigations reviewed the complete
74-file selected inventory. No reportable vulnerability survived validation.
The scan is scoped to declarations and header-defined behavior; it is not a
review of all compiled CPU implementation files or every installed consumer.

TArray's count and byte-product bounds, relocation ownership, live-alias
handling, and inline storage were inspected against their allocator and
representative callers. A further deterministic test exchanges two inline
arrays and an inline array with a heap array, checking values, object counts,
self-swap, and subsequent append. The candidate Release suite passed 49/49;
the ASan/UBSan runner returned `PASS`. These checks do not establish every
possible caller precondition or exception-enabled instantiation.

The review identified no current CPU use of `SkAnySubclass`. Its generic
template has only eight-byte inline alignment and assumes the base subobject
begins at the stored address. Current uses are in excluded GPU code, with no
demonstrated failing derived type. If GPU support is admitted, that contract
requires a fresh review or a stronger implementation before use.
The current CPU package also omits this GPU-only header from installation.

The sealed report and canonical coverage remain in the local Codex Security
workbench. The full CPU raster library conversion and security review remain
open in `docs/CONVERSION.md`.
