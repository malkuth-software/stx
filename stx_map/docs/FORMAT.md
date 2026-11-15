# TMAP 2

A TMAP 2 file holds up to sixteen layers of integer polylines (open lines or
closed rings) coded as turtle chains with an adaptive binary range coder.
`tools/bake/tmap_codec.js` writes it, `tools/bake/tmap_decode.js` is the
reference reader and `src/decode.cpp` the C++ reader; the two readers give
the same positions, which `tests/map.cpp` checks.

All integers are little-endian. Positions are integers in units of
1/`units_per_degree` degree (32768 in the shipped map), x from 180 degrees
west, y from 90 degrees south; y grows northward.

## Layout

| Bytes | Field |
|---|---|
| 4 | `TMAP` |
| 1 | version, 2 |
| 1 | mantissa bits, 0..4 |
| 2 | length unit, in position units |
| 16 | log2 of the turn bins for length classes 0..15, each 0..12 |
| 4 | units per degree |
| 4 | tolerance, in position units: every decoded line lies within this (symmetric Hausdorff distance) of its source line |
| 1 | layer count, 1..16 |
| | one record per layer (below) |
| | each layer's body, in layer order |
| 4 | CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320) of every byte before it |

A layer record:

| Bytes | Field |
|---|---|
| 1 | kind: 0 rings (closed; the closing segment is implied), 1 lines |
| 1 | flags: bit 0 optional (decoded only when asked for by name); other bits 0 |
| 1 | name length, 1..32 |
| n | name, characters `a`-`z`, `0`-`9`, `_` |
| 2 | start grid, in position units |
| 2 | depends: bit i set when this layer's ties name places in layer i; only earlier layers |
| 4 | lines |
| 4 | vertices, at least the line count |
| 4 | ties, at most twice the line count |
| 4 | body bytes |

A reader checks the CRC, then every header field, then that the bodies
exactly fill the space between the records and the CRC, before decoding
anything. The readers refuse more than 2^22 vertices over all layers (the
C++ reader's `DecodeLimits` may lower that).

## The range coder

Each body is one range-coded stream, LZMA style. The state is a 31-bit
range (initially 0x7FFFFFFF) and a code; the code starts as the first four
body bytes, big-endian, and a code with its top bit set is refused. Bytes
past the body's end read as zero. After every decision, while the range is
below 2^23 it is shifted left 8 bits and the next byte is shifted into the
code.

A modelled bit uses a probability p (15 bits, the chance of a 0, initially
16384) and an update count: bound = (range >> 15) * p. A code below bound
is a 0 (range = bound, p += (32768 - p) >> s), otherwise a 1 (code -= bound,
range -= bound, p -= p >> s). The shift s is 1 on a probability's first use
and grows by one per use up to 5, so it moves by a half, a quarter, ... and
settles at 1/32.

A raw field of n bits is read 8 bits at a time at most: range >>= k for a
k-bit piece, the piece is code / range (refused if it does not fit in k
bits), code -= piece * range, then the range is renormalised.

A tree of n bits reads n modelled bits from node 1 down, node = 2 * node +
bit, using the probability at base + node; the value is node - 2^n.

## A layer's lines

Lines are stored in the order of the Hilbert index (order 15) of their
start cells. For each line:

- **Start.** The gap g from the previous line's Hilbert index (0 for the
  first): its bit length k as a 5-bit tree (base 0 in the gap-length
  model); then min(k - 1, 2) bits below the leading one as a tree at base
  4k in the gap-bits model; then the remaining low bits raw. The index must
  stay below 2^30. Its cell (x, y), times the start grid, is the first
  vertex.
- **Steps.** Before each step, an end flag (modelled, context min(n, 15)
  for the step number n): 1 ends the line. Then the length class e as a
  4-bit tree at base 16 * (previous step's class), the mantissa m as a tree
  of the mantissa bits at base 16e, and the turn in b = bins[e] bits:
  - the first step's turn is raw (b bits, an absolute heading);
  - a later step's turn magnitude has bit length k, a 4-bit tree at base
    16c with context c = 3e + s (s: 0 after a zero turn, 1 when the previous
    k + 3 <= its b, otherwise 2); when k > 0, a side bit (modelled, context
    c) XOR the previous side gives the sign, then min(k - 1, 2) bits below
    the leading one as a tree at base 4 * (16e + k), then the rest raw.
- The step's length is m units for e = 0, otherwise (2^mantissa_bits + m) <<
  (e - 1) units, times the length unit. The heading (of 4096 per turn)
  becomes (heading + turn * (4096 >> b)) mod 4096, starting from 0 for each
  line. The step moves by floor((length * C + 8192) / 16384) in x and the
  same with S in y, where C and S are round(cos and sin of 2 pi heading /
  4096, times 16384) (`src/heading_table.inc`). Every position must lie
  within 2^28 units of the origin.

The models are fresh for each layer and shared by its lines; the decoded
vertex count must equal the record's.

## Ties

After the lines, the layer's ties, all raw bits, applied in stored order:

| Bits | Field |
|---|---|
| ceil(log2(lines)) | line (stored index) |
| 1 | end: 0 the first vertex, 1 the last |
| 4 | target layer: this layer or one in its depends mask |
| ceil(log2(target vertices)) | target vertex v |
| 1 | fraction flag |
| 8, when flagged | fraction f, 1..255 |

The line's end vertex takes the target's position: vertex v, or with a
fraction the point f/256 of the way to vertex v + 1, x(v) + floor(((x(v+1)
- x(v)) * f + 128) / 256) and the same in y. The target vertex (and v + 1)
must exist. A tie onto the same layer reads positions as already tied.

The bake ties the ends of lines that met in the source (a border that ended
on a coast, or two borders sharing an end), so they meet exactly when
decoded, and drops any tie that would take a line past the tolerance.

## Coarser levels

The file holds only the finest level. A reader derives coarser ones at load
(`levels.hpp`): integer Douglas-Peucker with exact comparisons, line ends
always kept, rings split at the vertex farthest from their start.
