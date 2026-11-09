"""Paired introsort costs with explicit reset work."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    output = subprocess.check_output(
        [f"build/{profile}/stx_skia_sort_tests", "--benchmark"], text=True)
    result = {}
    for line in output.splitlines():
        name, iterations, seconds, digest = line.split(",")
        result[name] = {"iterations": int(iterations), "seconds": float(seconds), "digest": digest}
    return result


def main():
    for profile in ("baseline", "candidate"):
        run(profile)
    trials = {"baseline": [], "candidate": []}
    for index in range(7):
        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
        for profile in order:
            trials[profile].append(run(profile))
    rows = []
    for name in trials["baseline"][0]:
        digests = {trial[name]["digest"] for series in trials.values() for trial in series}
        assert len(digests) == 1
        iterations = trials["baseline"][0][name]["iterations"]
        times = {profile: [trial[name]["seconds"] for trial in trials[profile]] for profile in trials}
        ratio = statistics.median(times["candidate"]) / statistics.median(times["baseline"])
        rows.append({"workload": name, "iterations": iterations, "seconds": times,
                     "candidate_over_reference_median": ratio, "digest": next(iter(digests))})
        print(name, "ratio", round(ratio, 3), "ns/iteration",
              round(statistics.median(times["baseline"]) * 1e9 / iterations, 2),
              round(statistics.median(times["candidate"]) * 1e9 / iterations, 2))
    result = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "measurements": rows,
        "scope": "One unmeasured run per profile, then seven alternating paired Release trials. "
                 "262144 eight-byte records per workload, sizes 32/256/4096. "
                 "Ascending, descending, equal, few-key, permuted and organ-pipe patterns. "
                 "Includes resetting the input and endpoint hashing; not renderer throughput."}

    destination = Path("build/sort-measurements.json")
    if destination.exists():
        index = 1
        while Path(f"build/sort-measurements-prior-{index}.json").exists():
            index += 1
        destination.rename(f"build/sort-measurements-prior-{index}.json")
    destination.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
