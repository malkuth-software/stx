"""Bounded raw-array microbenchmarks; does not measure renderer throughput."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    output = subprocess.check_output(
        [f"build/{profile}/stx_skia_td_array_tests", "--benchmark"], text=True)
    lines = [line.split() for line in output.splitlines()]
    assert lines[0][3] == "499999500000"
    assert lines[1][3] == "8"
    return {"reserved_push_ns": float(lines[0][1]), "insert_erase_ns": float(lines[1][1]),
            "storage_bytes": int(lines[0][5]), "array_bytes": int(lines[0][7])}


def main():
    for profile in ("baseline", "candidate"):
        run(profile)
    trials = {"baseline": [], "candidate": []}
    for index in range(7):
        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
        for profile in order:
            trials[profile].append(run(profile))
    for name in ("reserved_push_ns", "insert_erase_ns"):
        medians = {profile: statistics.median(trial[name] for trial in series)
                   for profile, series in trials.items()}
        print(name, medians, "ratio", medians["candidate"] / medians["baseline"])
    record = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "trials": trials,
        "scope": "One warmup per profile, seven alternating paired trials, 1000000 iterations. "
                 "Reserved integer push and external four-element insert/erase in an eight-element "
                 "array. Includes initial writes to reserved memory. No renderer speed claim."}
    # An earlier record is kept beside the new one, as the other measure_*.py tools keep theirs,
    # so a second run (tools/run_recorded_workloads.py) does not stop on it.
    destination = Path("build/td-array-measurements.json")
    if destination.exists():
        index = 1
        while Path(f"build/td-array-measurements-prior-{index}.json").exists():
            index += 1
        destination.rename(f"build/td-array-measurements-prior-{index}.json")
    destination.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
