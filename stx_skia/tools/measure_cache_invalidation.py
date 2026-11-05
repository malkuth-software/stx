"""Measure complete empty-cache invalidation administration against the reference."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    text = subprocess.check_output(
        [f"build/{profile}/stx_skia_resource_cache_tests", "--benchmark"], text=True)
    rows = {}
    for line in text.splitlines():
        caches, messages, iterations, elapsed, object_bytes = line.split(",")
        rows[(int(caches), int(messages))] = {
            "iterations": int(iterations), "seconds": float(elapsed),
            "cache_object_bytes": int(object_bytes)}
    return rows


def main():
    trials = {"baseline": [], "candidate": []}
    for index in range(7):
        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
        for profile in order:
            trials[profile].append(run(profile))
    rows = []
    for key in trials["baseline"][0]:
        baseline = [trial[key]["seconds"] for trial in trials["baseline"]]
        candidate = [trial[key]["seconds"] for trial in trials["candidate"]]
        ratio = statistics.median(candidate) / statistics.median(baseline)
        iterations = trials["baseline"][0][key]["iterations"]
        sizes = {profile: trials[profile][0][key]["cache_object_bytes"] for profile in trials}
        row = {"caches": key[0], "messages_per_iteration": key[1], "iterations": iterations,
               "baseline_seconds": baseline, "candidate_seconds": candidate,
               "candidate_over_reference_median": ratio, "cache_object_bytes": sizes}
        rows.append(row)
        print(key, "ratio", round(ratio, 3), "ns/iteration", round(statistics.median(baseline)*1e9/iterations, 2),
              round(statistics.median(candidate)*1e9/iterations, 2), "object bytes", sizes)
    result = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "measurements": rows,
        "scope": "Seven alternating paired trials after 20 warm-up iterations per case. "
                 "Each iteration posts the listed IDs then performs one missing-key lookup per "
                 "empty local cache, draining invalidations. Includes broadcast, locks, storage, "
                 "draining and empty lookup; excludes cache construction and record eviction. "
                 "Reference is the pinned original resource cache/message bus, so this measures "
                 "combined changes, not the isolated effect of buffer reuse. Not application latency."}
    Path("build/cache-invalidation-measurements.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
