"""Paired hash-table measurements and a bounded original full-table comparison."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    text = subprocess.check_output(
        [f"build/{profile}/stx_skia_hash_storage_tests", "--benchmark"], text=True)
    rows = {}
    for line in text.splitlines():
        mode, count, iterations, elapsed, digest, table_bytes, slot_bytes = line.split(",")
        rows[(mode, int(count))] = {"iterations": int(iterations), "seconds": float(elapsed),
            "digest": digest, "table_bytes": int(table_bytes), "slot_bytes": int(slot_bytes)}
    return rows


def main():
    candidate = subprocess.run(["build/candidate/stx_skia_hash_storage_tests", "--full-probe"],
                               capture_output=True, text=True, check=True, timeout=2)
    assert "Removing the sole entry" in candidate.stdout
    try:
        original = subprocess.run(["build/baseline/stx_skia_hash_storage_tests", "--full-probe"],
                                  capture_output=True, text=True, timeout=2)
    except subprocess.TimeoutExpired as error:
        assert b"Removing the sole entry" in error.stdout
        print("Original one-slot full-table deletion timed out after two seconds; candidate completed")
    else:
        raise RuntimeError(f"Original full-table probe unexpectedly returned {original.returncode}")
    trials = {"baseline": [], "candidate": []}
    for index in range(7):
        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
        for profile in order:
            trials[profile].append(run(profile))
    rows = []
    for key in trials["baseline"][0]:
        baseline = [trial[key]["seconds"] for trial in trials["baseline"]]
        candidate = [trial[key]["seconds"] for trial in trials["candidate"]]
        digests = {trial[key]["digest"] for runs in trials.values() for trial in runs}
        assert len(digests) == 1
        iterations = trials["baseline"][0][key]["iterations"]
        sizes = {profile: {name: trials[profile][0][key][name]
                          for name in ("table_bytes", "slot_bytes")} for profile in trials}
        ratio = statistics.median(candidate) / statistics.median(baseline)
        rows.append({"mode": key[0], "entries": key[1], "iterations": iterations,
            "baseline_seconds": baseline, "candidate_seconds": candidate,
            "candidate_over_reference_median": ratio, "sizes": sizes, "digest": next(iter(digests))})
        print(key, "ratio", round(ratio, 3), "ns/iteration",
              round(statistics.median(baseline)*1e9/iterations, 2),
              round(statistics.median(candidate)*1e9/iterations, 2), "sizes", sizes)
    result = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "measurements": rows,
        "scope": "Seven alternating paired trials. Tables are populated/reserved before timing. "
                 "Lookup alternates deterministic hits/misses; churn removes one key and inserts "
                 "a new key. Includes key hashing, probing and backward-shift deletion for "
                 "two-int values, with distributed and constant-hash collision controls. "
                 "Not application throughput, construction/growth cost or general value-type performance.",
        "full_table_probe": {"reference": "two-second timeout after entering removal", "candidate": "completed"}}
    destination = Path("build/hash-storage-measurements.json")
    if destination.exists():
        index = 1
        while Path(f"build/hash-storage-measurements-prior-{index}.json").exists():
            index += 1
        destination.rename(f"build/hash-storage-measurements-prior-{index}.json")
    destination.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
