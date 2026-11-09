# Integer-translation sampling without narrowing

The bilinear-to-nearest shortcut tested integer coordinates by casting each
translation to `int`. Finite transforms outside the representable integer range
made that test undefined. The shortcut now checks finiteness and compares each
binary32 coordinate with its binary32 truncation. This preserves the intended
integer-translation rule without introducing an integer representation limit.
The change applies once during shader setup, not inside the pixel loop.

Eight deterministic clamp cases cover signed translations just inside the int32
boundary, at 2^31, at 2^32 and at the largest finite float. Each renders a three-color
image and checks every destination pixel against the appropriate edge color.
The upstream fixture skips these cases because its narrowing has no defined
result in the out-of-range cases. The ordinary 16,128-scene sampling corpus retains
its previous output hash; common artifacts continue to match upstream.

The candidate release suite passes 57/57; the complete instrumented suite returns
PASS with diagnostics unread. The test is integrated into
`shader_coordinate_contract`, including the instrumented suite. House spelling
and lifetime checks report zero findings in both changed files. This fixes the
range issue identified during the image-shader conversion; strict subset handling
remains a separate focused follow-up.
