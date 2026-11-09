#!/usr/bin/env python3
"""Compares a SIMD profile's corpora with the scalar profile's (docs/ISA_PROFILES.md).

Usage: compare_isa.py <scalar tree> <simd tree> [--json <file>]

Every file named in tests/corpora.txt must exist in both trees. The instruction-set workload's
files (isa-workload*.raw) are read case by case as BGRA pixels; for each case the report gives
the pixels that differ and the largest difference in any one channel. Every other corpus file
is compared byte for byte.

tests/isa_bounds.txt states, per workload file and case, the largest channel difference
allowed against the scalar profile (and optionally how many pixels may differ), and which corpus
files other than the workload may differ at all. A file or case it does not name must match exactly. Prints one line per file or case
and exits 1 if a bound is exceeded or a file is missing, 2 on a usage error.
"""

import json
import pathlib
import struct
import sys

STX_SKIA_ROOT = pathlib.Path(__file__).resolve().parent.parent
NAME_BYTES = 32
WORKLOAD_PREFIX = "isa-workload"


def read_listed_names(path: pathlib.Path) -> list[str]:
    """Returns the output names tests/corpora.txt declares (every field after the program)."""
    names: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        names.extend(stripped.split()[1:])
    return names


def read_bounds(path: pathlib.Path) -> tuple[dict[tuple[str, str], tuple[int, int | None]], set[str]]:
    """Returns the per-case bounds (largest channel difference, and the most pixels that may
    differ or None for any number) and the set of other files allowed to differ.

    Lines are '<workload file> <case> <largest channel difference> [pixels=<n>] <reason...>'
    or '<corpus file> differs <reason...>'."""
    bounds: dict[tuple[str, str], tuple[int, int | None]] = {}
    differing: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split()
        if len(fields) >= 2 and fields[1] == "differs":
            differing.add(fields[0])
        elif len(fields) >= 3 and fields[2].isdigit():
            pixels = None
            if len(fields) >= 4 and fields[3].startswith("pixels=") and fields[3][7:].isdigit():
                pixels = int(fields[3][7:])
            bounds[(fields[0], fields[1])] = (int(fields[2]), pixels)
        else:
            raise ValueError(f"{path.name}: cannot read '{stripped}'")
    return bounds, differing


def read_cases(data: bytes) -> list[tuple[str, int, int, bytes]]:
    """Splits a workload file into (name, width, height, pixels) records."""
    cases: list[tuple[str, int, int, bytes]] = []
    offset = 0
    while offset < len(data):
        if offset + NAME_BYTES + 8 > len(data):
            raise ValueError("truncated case header")
        name = data[offset:offset + NAME_BYTES].split(b"\0", 1)[0].decode("ascii")
        width, height = struct.unpack_from("<II", data, offset + NAME_BYTES)
        start = offset + NAME_BYTES + 8
        end = start + width * height * 4
        if end > len(data):
            raise ValueError(f"truncated pixels in case {name}")
        cases.append((name, width, height, data[start:end]))
        offset = end
    return cases


def compare_pixels(scalar: bytes, simd: bytes) -> dict[str, object]:
    """Counts differing pixels and the per-channel (B, G, R, A) largest difference."""
    differing_pixels = 0
    largest = [0, 0, 0, 0]
    histogram = {"1": 0, "2": 0, "3-4": 0, "5+": 0}
    for index in range(0, len(scalar), 4):
        if scalar[index:index + 4] == simd[index:index + 4]:
            continue
        differing_pixels += 1
        pixel_largest = 0
        for channel in range(4):
            difference = abs(scalar[index + channel] - simd[index + channel])
            largest[channel] = max(largest[channel], difference)
            pixel_largest = max(pixel_largest, difference)
        if pixel_largest == 1:
            histogram["1"] += 1
        elif pixel_largest == 2:
            histogram["2"] += 1
        elif pixel_largest <= 4:
            histogram["3-4"] += 1
        else:
            histogram["5+"] += 1
    return {"differing_pixels": differing_pixels, "pixels": len(scalar) // 4,
            "largest_bgra": largest, "largest": max(largest), "histogram": histogram}


def compare(scalar_tree: pathlib.Path, simd_tree: pathlib.Path, root: pathlib.Path) -> tuple[int, dict]:
    corpus_names = read_listed_names(root / "tests" / "corpora.txt")
    bounds, differing = read_bounds(root / "tests" / "isa_bounds.txt")
    failures = 0
    report: dict[str, object] = {"files": {}, "cases": {}}
    for name in corpus_names:
        scalar_file = scalar_tree / name
        simd_file = simd_tree / name
        if not scalar_file.is_file() or not simd_file.is_file():
            print(f"FAIL {name}: missing from a profile")
            failures += 1
            continue
        scalar_bytes = scalar_file.read_bytes()
        simd_bytes = simd_file.read_bytes()
        if name.startswith(WORKLOAD_PREFIX):
            scalar_cases = read_cases(scalar_bytes)
            simd_cases = read_cases(simd_bytes)
            if [(c[0], c[1], c[2]) for c in scalar_cases] != [(c[0], c[1], c[2]) for c in simd_cases]:
                print(f"FAIL {name}: the profiles wrote different cases")
                failures += 1
                continue
            for scalar_case, simd_case in zip(scalar_cases, simd_cases):
                result = compare_pixels(scalar_case[3], simd_case[3])
                bound, pixel_bound = bounds.get((name, scalar_case[0]), (0, None))
                result["bound"] = bound
                result["pixel_bound"] = pixel_bound
                report["cases"][f"{name}:{scalar_case[0]}"] = result
                within = result["largest"] <= bound and (
                    pixel_bound is None or result["differing_pixels"] <= pixel_bound)
                verdict = "ok" if within else "FAIL"
                if verdict == "FAIL":
                    failures += 1
                pixel_note = "" if pixel_bound is None else f", at most {pixel_bound} pixels"
                print(f"{verdict} {name} {scalar_case[0]}: {result['differing_pixels']}/{result['pixels']} "
                      f"pixels differ, largest {result['largest']} (BGRA {result['largest_bgra']}), "
                      f"bound {bound}{pixel_note}, histogram {result['histogram']}")
            continue
        differing_bytes = 0
        largest_byte = 0
        for scalar_byte, simd_byte in zip(scalar_bytes, simd_bytes):
            if scalar_byte != simd_byte:
                differing_bytes += 1
                largest_byte = max(largest_byte, abs(scalar_byte - simd_byte))
        differing_bytes += abs(len(scalar_bytes) - len(simd_bytes))
        report["files"][name] = {"differing_bytes": differing_bytes, "bytes": len(scalar_bytes),
                                 "largest_byte_difference": largest_byte, "allowed": name in differing}
        # The largest byte difference reads as a channel difference only in files of 8-bit
        # pixels; in files that hold floats or integers it says only that bytes moved.
        detail = f"{differing_bytes}/{len(scalar_bytes)} bytes, largest byte difference {largest_byte}"
        if differing_bytes == 0:
            print(f"identical {name}")
        elif name in differing:
            print(f"differs (allowed) {name}: {detail}")
        else:
            print(f"FAIL {name}: {detail} from the scalar profile")
            failures += 1
    print(f"{len(corpus_names)} corpus files, {failures} failures")
    report["failures"] = failures
    return failures, report


def main(arguments: list[str]) -> int:
    json_path = None
    if len(arguments) == 4 and arguments[2] == "--json":
        json_path = pathlib.Path(arguments[3])
        arguments = arguments[:2]
    if len(arguments) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    failures, report = compare(pathlib.Path(arguments[0]), pathlib.Path(arguments[1]), STX_SKIA_ROOT)
    if json_path is not None:
        json_path.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    if failures != 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
