#!/usr/bin/env python3
"""Times the instruction-set workload in several profile trees (docs/ISA_PROFILES.md).

Usage: measure_isa.py [--trials N] [--json <file>] <tree> [<tree> ...]

Runs <tree>/stx_skia_isa_workload_tests --benchmark once per tree per trial, rotating the
order each trial so no tree always runs first. Each run prints, per case and precision, the
median frame time of a 1280x800 BGRA frame. The report gives the median over trials and each
tree's ratio to the first tree (below one is faster). A tree is named by its directory.

Timings are only as good as the host: a shared runner's noise floor is several percent, so a
ratio within about 0.06 of one says nothing either way.
"""

import json
import pathlib
import platform
import statistics
import subprocess
import sys

DEFAULT_TRIALS = 7
PROGRAM = "stx_skia_isa_workload_tests"


def host_description() -> str:
    """The processor's marketing name where the platform exposes it, else the machine type."""
    system = platform.system()
    try:
        if system == "Darwin":
            name = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
            return name
        if system == "Linux":
            for line in pathlib.Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except (OSError, subprocess.CalledProcessError):
        pass
    return platform.processor() or platform.machine()


def run(tree: pathlib.Path) -> dict[tuple[str, str], float]:
    executable = tree / PROGRAM
    if not executable.exists():
        executable = tree / (PROGRAM + ".exe")
    output = subprocess.check_output([str(executable), "--benchmark"], text=True)
    rows: dict[tuple[str, str], float] = {}
    for line in output.splitlines()[1:]:
        case, precision, median_ns, _ = line.split(",")
        rows[(case, precision)] = float(median_ns)
    return rows


def main(arguments: list[str]) -> int:
    trials = DEFAULT_TRIALS
    json_path = None
    trees: list[pathlib.Path] = []
    index = 0
    while index < len(arguments):
        if arguments[index] == "--trials" and index + 1 < len(arguments):
            trials = int(arguments[index + 1])
            index += 2
        elif arguments[index] == "--json" and index + 1 < len(arguments):
            json_path = pathlib.Path(arguments[index + 1])
            index += 2
        else:
            trees.append(pathlib.Path(arguments[index]))
            index += 1
    if not trees or trials < 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    names = [tree.name for tree in trees]
    samples: dict[str, list[dict[tuple[str, str], float]]] = {name: [] for name in names}
    for trial in range(trials):
        shift = trial % len(trees)
        for position in range(len(trees)):
            tree = trees[(position + shift) % len(trees)]
            samples[tree.name].append(run(tree))
    keys = list(samples[names[0]][0].keys())
    medians = {name: {key: statistics.median(sample[key] for sample in samples[name]) for key in keys}
               for name in names}
    header = "| Case | Precision | " + " | ".join(f"{name} ms" for name in names)
    header += "".join(f" | {name}/{names[0]}" for name in names[1:]) + " |"
    print(f"Host: {host_description()}, {platform.system()} {platform.machine()}; {trials} trials")
    print(header)
    print("|" + "---|" * (2 + len(names) + len(names) - 1))
    rows = []
    for case, precision in keys:
        values = [medians[name][(case, precision)] for name in names]
        ratios = [value / values[0] for value in values[1:]]
        cells = " | ".join(f"{value / 1e6:.3f}" for value in values)
        ratio_cells = "".join(f" | {ratio:.2f}" for ratio in ratios)
        print(f"| {case} | {precision} | {cells}{ratio_cells} |")
        rows.append({"case": case, "precision": precision,
                     "median_ns": {name: medians[name][(case, precision)] for name in names}})
    if json_path is not None:
        report = {"host": host_description(), "system": platform.system(), "machine": platform.machine(),
                  "trials": trials, "trees": names, "rows": rows}
        json_path.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
