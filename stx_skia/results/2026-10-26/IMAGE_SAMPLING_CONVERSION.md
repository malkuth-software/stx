# Image-shader sampling and mutable coordinates

The image shader now uses named stage-building procedures and explicit state
instead of four capturing lambdas. Mipmap views, gather contexts, tile limits,
sampler weights and color-conversion plans retain their arena-backed lifetimes.
Sample ordering, filter coefficients, mip selection and pixel arithmetic remain
unchanged. Deserialization and factory ownership are explicit; unused legacy
enum and disabled example coefficients are removed.

The draw-image preparation result names its destination and owned shader. Two
canvas call sites consume those fields; this is a caller adaptation, not a claim
that SkCanvas is converted. Its spelling findings decrease from 494 to 492;
lifetime-rule checks report none. The four converted image/transform source and
header files and two tests have zero spelling/lifetime findings.

The mutable transform shader owns a fixed nine-coefficient array, documents the
pipeline borrow and rejects temporary child shaders. A separate preceding fix
propagates child stage-construction failure (TRANSFORM_CHILD_FAILURE.md).
The coordinate fixture verifies that updates affect an already-built pipeline,
rejected perspective updates preserve its previous matrix, and restoring identity
restores its output. Existing vertex and tiled-clip tests remain part of the gate.

## Expanded comparison and preserved differences

`image_sampling.cpp` exercises 16,128 scenes: all 28 admitted color formats,
all sixteen tile-mode pairs, 9x7/1x7/9x1 shapes with premultiplied/unpremultiplied/
opaque source conventions, seven sampling choices and regular/raw shader paths.
Choices include nearest, linear, nearest/linear mipmapping, Mitchell/Catmull-Rom
cubic and anisotropic fallback. Local matrices exercise half-pixel offsets,
reflections, minification and perspective. Destinations alternate sRGB and linear
sRGB. Raw cubic requests must fail. Source rows have padding; the RGBA destination
width is seventeen pixels.

The first expanded upstream comparison found 152 differing scenes, confined to
mipmapped RGBA10A2, BGRA10A2 and RG16. These match previously corrected mipmap
kernels (commits 53db238 and 403d46e; see MIPMAP_KERNELS.md). To establish that the
new conversion introduced no additional change, the entire 14,257,152-byte corpus
was rerun with our e1bb350 image-shader/header/canvas sources and the same current
candidate dependencies. All 16,128 scenes matched byte for byte. The authoritative
converted sources were then restored and rebuilt.

Ongoing comparison records the three corrected formats' mipmapped paths separately:

- `image-sampling.raw`: 15,264 common scenes, 13,493,376 bytes; candidate/reference
  byte-identical.
- `image-sampling-corrected.raw`: 864 scenes, 763,776 bytes; retained separately,
  not advertised as upstream equality. The existing mipmap tests independently
  check corrected packed-channel weighted sums.

Full initial outputs remain on the M4 under `build/image-sampling-before.raw` and
`build/image-sampling-converted.raw`. Local source snapshots and the exact replay
script/log are under `build/image-shader-before-verification/`. Before/after
canvas style logs are under `build/image-shader-style/`.

## Gates and limits

Non-Apple LLVM on the M4, using the sequential AGENTS.md build commands:

- Candidate release: 57/57 tests PASS.
- Reference release: 56/56 tests PASS.
- All fourteen common output files match byte for byte.
- Complete deterministic instrumented candidate suite: PASS; diagnostics unread.
- Image/transform translation units compile with `SK_DEBUG` and
  `SK_ENABLE_LEGACY_SHADERCONTEXT`; this is conditional compilation evidence,
  not a separate legacy/debug execution suite.

The broad comparison does not establish every subset or extreme-coordinate
behavior. Strict subset handling inherited from the image shader still needs a
focused contract check; the integer-translation sampling shortcut also needs its
out-of-range float-to-int conversion removed. Neither is silently treated as
covered by the common corpus. Pixel kernels, gradient sampling, bitmap-procedure
state and most canvas implementation remain separate conversion work. No drawing
speed improvement is claimed here.
