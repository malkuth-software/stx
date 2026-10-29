"""Paired pixel-conversion timings, excluding allocation and file I/O."""
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess


def benchmark(profile):
    result = subprocess.check_output(
        [f"build/{profile}/stx_skia_pixel_conversion_tests", "--benchmark"], text=True)
    rows = {}
    for line in result.splitlines():
        name, width, height, repeats, trial, seconds = line.split(",")
        key = f"{name}:{width}x{height}"
        entry = rows.setdefault(key, {"repeats": int(repeats), "seconds_per_call": []})
        entry["seconds_per_call"].append(float(seconds) / int(repeats))
    return rows


def main():
    reference = Path("build/baseline/conversions.raw").read_bytes()
    candidate = Path("build/candidate/conversions.raw").read_bytes()
    if reference != candidate:
        raise RuntimeError("Pixel-conversion outputs differ")
    reports = {"baseline": {}, "candidate": {}}
    for pair in range(3):
        order = ["baseline", "candidate"]
        if pair % 2:
            order.reverse()
        for profile in order:
            rows = benchmark(profile)
            for key, values in rows.items():
                entry = reports[profile].setdefault(key, [])
                entry.extend(values["seconds_per_call"])
    sources = {}
    # The reference sources are hashed only where a checkout provides them: the stx_skia
    # tree itself no longer carries baseline/.
    for name in ["SkConvertPixels.cpp", "SkConvertPixels.h", "SkRectMemcpy.h"]:
        for directory in ["skia/src/core", "baseline/skia/src/core"]:
            path = Path(directory) / name
            if path.is_file():
                sources[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    report = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
        "output_bytes": len(candidate),
        "output_sha256": hashlib.sha256(candidate).hexdigest(),
        "source_sha256": sources,
        "seconds_per_call": reports,
        "limits": "Warm reused buffers, three-pixel row padding, fixed input values. Three alternating profile pairs with five timing trials per workload. Excludes allocations and I/O. Not whole application speed or a broad performance corpus."
    }
    Path("build/pixel-conversion-measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    for key in reports["baseline"]:
        before = statistics.median(reports["baseline"][key])
        after = statistics.median(reports["candidate"][key])
        print(f"{key}: reference={before * 1e6:.3f} us candidate={after * 1e6:.3f} us ratio={after/before:.3f}")


if __name__ == "__main__":
    main()
