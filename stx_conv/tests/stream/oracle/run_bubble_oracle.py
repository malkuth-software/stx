"""Generates the bubble oracle fixture and runs the stream's tolerance check.

Usage: python3 run_bubble_oracle.py CHECK_PROGRAM FIXTURE
Exits 77 (reported by CTest as skipped) when NumPy is not available.
The fixture holds, per case, seven little-endian int64 header fields
(sw, sh, tw, th, channels, sampling 0 nodal / 1 pixel_area, precision bits)
followed by the bubble model's output; the check program regenerates the
inputs itself from regression_cases.cpp.

Generating the fixture takes about 90 s on an Apple M4, so it is kept beside
FIXTURE with a key file holding the SHA-256 of the oracle's Python sources;
it is regenerated only when that key is missing or differs.
"""
import hashlib
import os
import struct
import subprocess
import sys

try:
    import numpy  # noqa: F401
except ImportError:
    print("SKIP: NumPy is not available to this Python; the bubble oracle was not run")
    sys.exit(77)

import cases
import common
import stream_model


def write_fixture(path):
    entries = list(cases.cases())
    temporary = path + ".partial"
    with open(temporary, "wb") as out:
        out.write(struct.pack("<q", len(entries)))
        for index, sw, sh, tw, th, channels, sampling, pattern, precision, seed in entries:
            image, dtype = common.case_image(sw, sh, channels, pattern, precision, seed)
            output = common.expected(image, tw, th, sampling, dtype, stream_model.bubble_line)
            sampling_code = 0 if sampling == "nodal" else 1
            out.write(struct.pack("<qqqqqqq", sw, sh, tw, th, channels, sampling_code, precision))
            out.write(output.tobytes())
    os.replace(temporary, path)


ORACLE_SOURCES = ("cases.py", "common.py", "plan_model.py", "stream_model.py", "run_bubble_oracle.py")


def oracle_key():
    digest = hashlib.sha256()
    here = os.path.dirname(os.path.abspath(__file__))
    for name in ORACLE_SOURCES:
        with open(os.path.join(here, name), "rb") as source:
            digest.update(name.encode("ascii") + b"\0" + source.read())
    return digest.hexdigest()


def fixture_is_current(path, key):
    try:
        with open(path + ".key", encoding="ascii") as stored:
            current = stored.read().strip() == key and os.path.isfile(path)
    except OSError:
        current = False
    return current


def main():
    if len(sys.argv) != 3:
        print("usage: run_bubble_oracle.py CHECK_PROGRAM FIXTURE", file=sys.stderr)
        return 2
    key = oracle_key()
    if not fixture_is_current(sys.argv[2], key):
        write_fixture(sys.argv[2])
        with open(sys.argv[2] + ".key", "w", encoding="ascii") as stored:
            stored.write(key + "\n")
    completed = subprocess.run([sys.argv[1], sys.argv[2]], check=False)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
