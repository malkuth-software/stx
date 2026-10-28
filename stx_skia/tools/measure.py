"""Paired native rendering and pixel-transfer measurements; no file I/O timing."""
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import subprocess


def run(command):
    return subprocess.check_output(command, text=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    baseline = Path("build/baseline")
    candidate = Path("build/candidate")
    if (baseline / "render.raw").read_bytes() != (candidate / "render.raw").read_bytes():
        raise RuntimeError("Rendering differs")
    times = {"baseline": [], "candidate": []}
    hashes = set()
    run([str(baseline / "stx_skia_render_tests")])
    run([str(candidate / "stx_skia_render_tests")])
    for trial in range(9):
        order = ["baseline", "candidate"]
        if trial % 2:
            order.reverse()
        for name in order:
            result = run([str(Path("build") / name / "stx_skia_render_tests")])
            hashes.add(re.search(r"hash=(\d+)", result).group(1))
            times[name].append(float(re.search(r"draw_seconds=([\d.]+)", result).group(1)))
    if len(hashes) != 1:
        raise RuntimeError("Repeated rendering differs")
    storage = run([str(candidate / "stx_skia_storage_tests"), "--benchmark"])
    statistics_rows = {}
    for line in storage.splitlines():
        if not re.match(r"^\d+,", line):
            continue
        size, trial, copied, transferred = line.split(",")
        entry = statistics_rows.setdefault(size, {"copied": [], "transferred": []})
        entry["copied"].append(float(copied))
        entry["transferred"].append(float(transferred))
    report = {"platform": platform.platform(), "machine": platform.machine(),
              "compiler": run(["clang++", "--version"]).splitlines()[0],
              "render_cases_per_trial": 24, "render_seconds": times,
              "render_raw_sha256": digest(candidate / "render.raw"),
              "storage_100_productions_seconds": statistics_rows,
              "artifacts": {}, "limits": "In-memory primitive suite. Not whole application latency or peak RSS. Storage trials include vector allocation/fill and data destruction, not decoding. Current host only."}
    for name in ["baseline", "candidate"]:
        directory = Path("build") / name
        report["artifacts"][name] = {}
        for file in ["libstx_skia.a", "stx_skia_render_tests"]:
            path = directory / file
            report["artifacts"][name][file] = {"bytes": path.stat().st_size, "sha256": digest(path)}
        commands = json.loads((directory / "compile_commands.json").read_text())
        count = 0
        for command in commands:
            if "stx_skia.dir" in command["command"]:
                count += 1
        report["artifacts"][name]["library_compile_units"] = count
    Path("build/measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    for name, entries in times.items():
        print(name, "median draw seconds", statistics.median(entries))
    for size, entries in statistics_rows.items():
        print(size, "transfer/copy", statistics.median(entries["transferred"]) / statistics.median(entries["copied"]))
    print("PASS")


if __name__ == "__main__":
    main()
