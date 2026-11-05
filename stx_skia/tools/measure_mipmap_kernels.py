"""Paired steady-state mip-level measurements and compiler assembly evidence."""
import json
from pathlib import Path
import platform
import shlex
import statistics
import sys
import subprocess


def run(profile):
    result = subprocess.check_output(
        [f"build/{profile}/stx_skia_mipmap_tests", "--benchmark"], text=True)
    rows = {}
    for line in result.splitlines():
        color, width, height, iterations, elapsed, digest = line.split(",")
        rows[(int(color), int(width), int(height))] = {
            "iterations": int(iterations), "seconds": float(elapsed), "hash": digest}
    return rows


def main():
    measurements = {"baseline": [], "candidate": []}
    for trial in range(7):
        order = ("baseline", "candidate") if trial % 2 == 0 else ("candidate", "baseline")
        for profile in order:
            measurements[profile].append(run(profile))
    rows = []
    for key in measurements["baseline"][0]:
        baseline = [trial[key]["seconds"] for trial in measurements["baseline"]]
        candidate = [trial[key]["seconds"] for trial in measurements["candidate"]]
        hashes = {trial[key]["hash"] for trials in measurements.values() for trial in trials}
        assert len(hashes) == 1, (key, hashes)
        ratio = statistics.median(candidate) / statistics.median(baseline)
        rows.append({"format": key[0], "width": key[1], "height": key[2],
                     "iterations": measurements["baseline"][0][key]["iterations"],
                     "baseline_seconds": baseline, "candidate_seconds": candidate,
                     "candidate_over_reference_median": ratio, "hash": next(iter(hashes))})
        print(key, "candidate/reference", round(ratio, 3),
              "baseline range", round(min(baseline), 6), round(max(baseline), 6),
              "candidate range", round(min(candidate), 6), round(max(candidate), 6))
    record = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "measurements": rows,
        "scope": "Seven alternating paired runs; reused padded buffers, one mip level per call, "
                 "one warm-up per case. Allocations and factory creation excluded. "
                 "RGBA8 and RGBA F16 only; not complete pyramid or application latency."}
    Path("build/mipmap-kernels-measurements.json").write_text(json.dumps(record, indent=2) + "\n")


def emit_assembly():
    commands = json.loads(Path("build/candidate/compile_commands.json").read_text())
    entry = next(row for row in commands if row["file"].endswith("/SkMipmapHQDownSampler.cpp"))
    args = shlex.split(entry["command"])
    output = args.index("-o")
    del args[output:output + 2]
    args.remove("-c")
    args.extend(["-S", "-o", str(Path("build/mipmap-kernels-arm64.s").resolve())])
    subprocess.run(args, cwd=entry["directory"], check=True)
    args[-1] = str(Path("build/mipmap-kernels-sse2.s").resolve())
    sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    args.extend(["--target=x86_64-apple-macos14", "-isysroot", sdk,
                 "-march=x86-64", "-mtune=generic", "-msse2", "-mno-sse3",
                 "-mno-ssse3", "-mno-sse4.1", "-mno-sse4.2", "-mno-avx", "-mno-avx2"])
    subprocess.run(args, cwd=entry["directory"], check=True)
    print("Native and SSE2 assembly emitted")


if __name__ == "__main__":
    # --no-assembly times only; the assembly step cross-compiles an SSE2 preview against the
    # macOS SDK, so it runs only on a Mac.
    if sys.argv[1:] == ["--assembly-only"]:
        emit_assembly()
    elif sys.argv[1:] == ["--no-assembly"]:
        main()
    else:
        main()
        emit_assembly()
