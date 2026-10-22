"""Check encoder output independently with Python's zlib runtime."""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib


def main():
    count = 0
    block_types = [0, 0, 0]
    with tempfile.TemporaryDirectory(prefix="stx-deflate-") as directory:
        path = pathlib.Path(directory) / "records.bin"
        subprocess.run([sys.argv[1], str(path)], check=True)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        with path.open("rb") as records:
            while header := records.read(12):
                assert len(header) == 12
                input_size, encoded_size, flags = struct.unpack("<III", header)
                original = records.read(input_size)
                encoded = records.read(encoded_size)
                assert len(original) == input_size and len(encoded) == encoded_size
                decoder = zlib.decompressobj(-15 if flags & 1 else 15)
                decoded = decoder.decompress(encoded) + decoder.flush()
                assert decoded == original, count
                assert decoder.eof == (not flags & 2), count
                assert not decoder.unused_data and not decoder.unconsumed_tail, count
                first_byte = encoded[0 if flags & 1 else 2]
                block_type = (first_byte >> 1) & 3
                assert block_type < 3
                block_types[block_type] += 1
                count += 1
    assert all(block_types), "stored, fixed and dynamic blocks must each be exercised"
    report = {"oracle_zlib": zlib.ZLIB_RUNTIME_VERSION, "records": count,
              "records_sha256": digest, "first_block_types": block_types}
    if len(sys.argv) == 3:
        pathlib.Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + "\n")
    print(f"Independent zlib {zlib.ZLIB_RUNTIME_VERSION}: {count} records passed")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
