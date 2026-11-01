"""Shared case evaluation for the stream oracle scripts (requires NumPy)."""
import numpy as np

import cases
import stream_model


def case_image(sw, sh, channels, pattern, precision, seed):
    dtype = np.float32 if precision == 32 else np.float64
    values = np.array(cases.image(pattern, sw, sh, channels, seed), dtype=np.float64)
    return values.astype(dtype).reshape(sh, sw, channels), dtype


def expected(image, tw, th, sampling, dtype, line):
    result = stream_model.resize(image, tw, th, sampling, dtype, line)
    little_endian = np.dtype(dtype).newbyteorder("<")
    return np.ascontiguousarray(result, dtype=little_endian)
