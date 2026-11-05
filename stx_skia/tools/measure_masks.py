"""Paired mask blur timing, including allocation and temporary storage."""
import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rect", action="store_true")
    args = parser.parse_args()
    mode = "--benchmark-rect" if args.rect else "--benchmark"
    rows = {}
    checksums = set()
    for trial in range(9):
        profiles = ["baseline", "candidate"]
        if trial % 2:
            profiles.reverse()
        for profile in profiles:
            result = subprocess.check_output(
                [str(Path("build") / profile / "stx_skia_mask_tests"), mode],
                text=True)
            for line in result.splitlines():
                if line.startswith("checksum="):
                    checksums.add(line)
                    continue
                fields = line.split(",")
                key = ",".join(fields[:5])
                entry = rows.setdefault(key, {"baseline": [], "candidate": []})
                entry[profile].append(float(fields[5]))
    if len(checksums) != 1:
        raise RuntimeError("Mask benchmark checksums differ")
    report = {
        "platform": platform.platform(),
        "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
        "key": ("style" if args.rect else "format") + ",width,height,sigma,iterations",
        "seconds": rows,
        "checksum": sorted(checksums),
        "mode": "analytic rectangle" if args.rect else "coverage mask",
        "scope": "Allocation, blur and destruction included; input preparation excluded. Nine alternating trials. Not whole GUI latency. Coverage mode uses padded source rows; rectangle mode constructs analytic profiles."
    }
    Path("build/rect-measurements.json" if args.rect else "build/mask-measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    for key, times in rows.items():
        baseline = statistics.median(times["baseline"])
        candidate = statistics.median(times["candidate"])
        print(key, "candidate/baseline", round(candidate / baseline, 4))


if __name__ == "__main__":
    main()
