"""Writes the base oracle digests checked by stx_conv_stream_regression.

Usage: python3 make_base_digests.py ../base_oracle.sha256 (requires NumPy).
One line per case: its index and the SHA-256 of the base model's output in
little-endian binary32 or binary64. The `inputs` line digests every case's
input in order, so the C++ generator is checked against cases.py too.
"""
import hashlib
import sys

import numpy as np

import cases
import common
import stream_model


def main():
    lines = []
    inputs = hashlib.sha256()
    for index, sw, sh, tw, th, channels, sampling, pattern, precision, seed in cases.cases():
        image, dtype = common.case_image(sw, sh, channels, pattern, precision, seed)
        little_endian = np.dtype(dtype).newbyteorder("<")
        inputs.update(np.ascontiguousarray(image, dtype=little_endian).tobytes())
        output = common.expected(image, tw, th, sampling, dtype, stream_model.base_line)
        lines.append(f"{index} {hashlib.sha256(output.tobytes()).hexdigest()}")
    with open(sys.argv[1], "w", encoding="ascii", newline="\n") as out:
        out.write("# SHA-256 of tests/stream/oracle/stream_model.py base_line output per case;\n")
        out.write("# written by make_base_digests.py. Do not edit by hand.\n")
        out.write(f"inputs {inputs.hexdigest()}\n")
        out.write("\n".join(lines) + "\n")
    print(f"{len(lines)} cases written")


if __name__ == "__main__":
    main()
