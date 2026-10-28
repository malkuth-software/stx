"""Paired uncontended synchronization costs."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    output = subprocess.check_output(
        [f"build/{profile}/stx_skia_synchronization_tests", "--benchmark"], text=True)
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
                 "1000000 operations per workload. Semaphore wait/signal with one permit, "
                 "mutex scope, or completed-once call. Single-threaded, uncontended fast paths. "
                 "Not blocking/wakeup latency, fairness, contention scaling or renderer speed."}

    destination = Path("build/synchronization-measurements.json")
    if destination.exists():
        index = 1
        while Path(f"build/synchronization-measurements-prior-{index}.json").exists():
            index += 1
        destination.rename(f"build/synchronization-measurements-prior-{index}.json")
    destination.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
