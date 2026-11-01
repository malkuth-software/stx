"""The stream regression cases, generated without NumPy.

This module and tests/stream/regression_cases.cpp produce the same 2,112
images bit for bit. Every value comes from integer arithmetic and correctly
rounded binary64 addition, subtraction, multiplication and division, so no
platform library function (sin, pow, a NumPy generator) enters the inputs.
Binary32 cases round the binary64 value once, to nearest.

Case order: shape, channels (1, 3, 4), sampling (nodal, pixel_area),
pattern, precision (32, 64). Case k (from 0) uses seed k + 1.
Image layout: [row][column][channel], packed.
"""

MASK64 = (1 << 64) - 1

SHAPES = (
    (1, 1, 1, 1), (5, 5, 5, 5), (65, 5, 97, 9), (257, 5, 129, 7), (9, 7, 17, 13),
    (9, 7, 5, 4), (12, 5, 23, 9), (12, 6, 7, 3), (6, 6, 11, 2), (33, 31, 65, 61),
    (33, 31, 16, 15), (40, 24, 13, 7), (17, 40, 33, 79), (3, 20, 7, 41), (20, 20, 3, 11),
    (5, 9, 1, 1), (7, 30, 9, 6), (2, 12, 4, 25), (4, 4, 9, 9), (1, 8, 3, 15),
    (16, 16, 16, 31), (16, 16, 31, 16),
)
CHANNELS = (1, 3, 4)
SAMPLINGS = ("nodal", "pixel_area")
PATTERNS = ("noise", "gradient", "checker", "plateaus", "ties", "constant", "wide", "smooth")
PRECISIONS = (32, 64)
POWERS_OF_TEN = (1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0)


class SplitMix64:
    """Steele, Lea and Flood's SplitMix64; one 64-bit word per call."""

    def __init__(self, seed):
        self.state = seed & MASK64

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        return z ^ (z >> 31)

    def unit(self):
        """Uniform in [0, 1): the top 53 bits scaled by 2^-53, exactly."""
        return float(self.next() >> 11) * (1.0 / 9007199254740992.0)


def cases():
    """Yields (index, sw, sh, tw, th, channels, sampling, pattern, precision, seed)."""
    index = 0
    for sw, sh, tw, th in SHAPES:
        for channels in CHANNELS:
            for sampling in SAMPLINGS:
                for pattern in PATTERNS:
                    for precision in PRECISIONS:
                        yield index, sw, sh, tw, th, channels, sampling, pattern, precision, index + 1
                        index += 1


def pixel(pattern, x, y, channel, width, height, channels, random):
    if pattern == "noise":
        return random.unit()
    if pattern == "gradient":
        return float(x + 2 * y + channel) / float(width + 2 * height + channels)
    if pattern == "checker":
        return float((x + y + channel) % 2)
    if pattern == "plateaus":
        drawn = random.unit()
        return 0.3 if y < height // 2 + x % 3 else drawn
    if pattern == "ties":
        return float(random.next() % 4) / 3.0
    if pattern == "constant":
        return 0.5
    if pattern == "wide":
        mantissa = 2.0 * random.unit() - 1.0
        exponent = int(random.next() % 13) - 6
        if exponent >= 0:
            return mantissa * POWERS_OF_TEN[exponent]
        return mantissa / POWERS_OF_TEN[-exponent]
    # smooth: a product of two parabolic waves.
    tx = float((7 * x + 3 * channel) % 20) / 10.0 - 1.0
    ty = float((5 * y) % 16) / 8.0 - 1.0
    a = 1.0 - tx * tx
    b = 1.0 - ty * ty
    return 0.5 + 0.4 * (a * b)


def image(pattern, width, height, channels, seed):
    """Binary64 values, [row][column][channel]; channel planes drawn in turn."""
    random = SplitMix64(seed)
    values = [0.0] * (width * height * channels)
    for channel in range(channels):
        for y in range(height):
            for x in range(width):
                values[(y * width + x) * channels + channel] = pixel(
                    pattern, x, y, channel, width, height, channels, random)
    return values
