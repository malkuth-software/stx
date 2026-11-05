"""Paired owned-byte and primitive-reader costs with current dependencies."""
import json
from pathlib import Path
import platform
import statistics
import subprocess


def run(profile):
    output = subprocess.check_output(
        [f"build/{profile}/stx_skia_read_buffer_tests", "--benchmark"], text=True)
    result = {}
    for line in output.splitlines():
        name, iterations, seconds, digest = line.split(",")
        result[name] = {"iterations": int(iterations), "seconds": float(seconds), "digest": digest}
    return result


def main():
    for profile in ("read-buffer-prior", "candidate"):
        run(profile)
    trials = {"read-buffer-prior": [], "candidate": []}
    for index in range(7):
        order = ("read-buffer-prior", "candidate") if index % 2 == 0 else ("candidate", "read-buffer-prior")
        for profile in order:
            trials[profile].append(run(profile))
    rows = []
    for name in trials["read-buffer-prior"][0]:
        digests = {trial[name]["digest"] for series in trials.values() for trial in series}
        assert len(digests) == 1
        iterations = trials["read-buffer-prior"][0][name]["iterations"]
        times = {profile: [trial[name]["seconds"] for trial in trials[profile]] for profile in trials}
        ratio = statistics.median(times["candidate"]) / statistics.median(times["read-buffer-prior"])
        rows.append({"workload": name, "iterations": iterations, "seconds": times,
                     "candidate_over_reference_median": ratio, "digest": next(iter(digests))})
        print(name, "ratio", round(ratio, 3), "ns/iteration",
              round(statistics.median(times["read-buffer-prior"]) * 1e9 / iterations, 2),
              round(statistics.median(times["candidate"]) * 1e9 / iterations, 2))
    result = {"platform": platform.platform(), "compiler": subprocess.check_output(
        ["clang++", "--version"], text=True).splitlines()[0], "measurements": rows,
        "scope": "One unmeasured run per profile, then seven alternating paired Release trials. "
                 "Only prior reader header/implementation differs; all dependencies are current. "
                 "Each owned-byte trial includes reader construction, allocation, copying, two "
                 "endpoint reads and destruction, 131072 times. Words trial reads 4096 words "
                 "8192 times, summing all values. This is not renderer throughput."}


    destination = Path("build/read-buffer-measurements.json")
    if destination.exists():
        index = 1
        while Path(f"build/read-buffer-measurements-prior-{index}.json").exists():
            index += 1
        destination.rename(f"build/read-buffer-measurements-prior-{index}.json")
    destination.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
