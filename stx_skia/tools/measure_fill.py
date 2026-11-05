"""Fill-kernel timings: the reference (original kernel) against the candidate.

Runs <reference>/stx_skia_memset_tests --benchmark and <candidate>/stx_skia_memset_tests
--benchmark alternately seven times, and reports the median of each row and the
candidate/reference ratio (below one is faster). Rows are the kernel as compiled
(kernel_default), the kernel after run-time dispatch (kernel_dispatched; the candidate
dispatches nothing, the reference may pick AVX or ERMS variants on x86), and canvas solid
fills. Writes <candidate>/fill-measurements.json.

Usage: python3 tools/measure_fill.py [reference build] [candidate build]
"""
import json
import pathlib
import platform
import statistics
import subprocess
import sys

TRIALS = 7


def run(build):
    output = subprocess.check_output([str(pathlib.Path(build) / "stx_skia_memset_tests"),
                                      "--benchmark"], text=True)
    rows = {}
    for line in output.splitlines()[1:]:
        kind, case, seconds, _ = line.split(",")
        rows[(kind, case)] = float(seconds)
    return rows


def main():
    reference = sys.argv[1] if len(sys.argv) > 1 else "build/baseline"
    candidate = sys.argv[2] if len(sys.argv) > 2 else "build/candidate"
    samples = {"reference": [], "candidate": []}
    for trial in range(TRIALS):
        order = ("reference", "candidate") if trial % 2 == 0 else ("candidate", "reference")
        for name in order:
            samples[name].append(run(reference if name == "reference" else candidate))
    rows = []
    for key in samples["reference"][0]:
        ref = statistics.median(trial[key] for trial in samples["reference"])
        cand = statistics.median(trial[key] for trial in samples["candidate"])
        rows.append({"kind": key[0], "case": key[1], "reference_seconds": ref,
                     "candidate_seconds": cand, "candidate_over_reference": cand / ref})
        print(f"{key[0]:>18} {key[1]:>15}  reference {ref * 1e6:10.2f} us  "
              f"candidate {cand * 1e6:10.2f} us  ratio {cand / ref:.3f}")
    record = {"platform": platform.platform(), "machine": platform.machine(),
              "processor": platform.processor(), "trials": TRIALS, "rows": rows}
    out = pathlib.Path(candidate) / "fill-measurements.json"
    out.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
