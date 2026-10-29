#!/usr/bin/env python3
"""Runs stx_skia's recorded timing workloads on one host (docs/ISA_PROFILES.md).

Usage: run_recorded_workloads.py [--output <directory>] [--isa <tree> ...]

Run from the stx_skia directory after tools/build_native.sh has built build/baseline and
build/candidate. Each optimisation the results records claim (docs/ISA_PROFILES.md lists
them) has a measure_*.py tool that alternates the reference and candidate builds; this runs
each of them in turn, then tools/measure_isa.py over build/candidate and any --isa trees.
Every tool's console output goes to <output>/<tool>.txt and every JSON it writes is copied to
<output>, with summary.md naming the host and each tool's result and duration. A tool that
fails is reported and the rest still run; the exit status is 1 if any failed.

Timings say something only beside their host and noise floor: the ratios a shared CI runner
gives are evidence for or against a claim, never a gate.
"""

import pathlib
import platform
import shutil
import subprocess
import sys
import time

# (tool, its arguments, the JSON files it writes under build/; each is copied to the output
# folder with '/' in its path replaced by '-'). measure_read_buffer.py is not here: it needs a
# build of the reader's prior source (tools/build_read_buffer_comparison.py), not the pin.
TOOLS = [
    ("measure_fill.py", [], ["candidate/fill-measurements.json"]),
    ("measure_swizzle.py", ["--no-code"], ["swizzle/measurements.json"]),
    ("measure_conversion.py", [], ["pixel-conversion-measurements.json"]),
    ("measure_masks.py", [], ["mask-measurements.json"]),
    ("measure_masks.py", ["--rect"], ["rect-measurements.json"]),
    ("measure_mipmap_kernels.py", ["--no-assembly"], ["mipmap-kernels-measurements.json"]),
    ("measure_hash_storage.py", [], ["hash-storage-measurements.json"]),
    ("measure_sort.py", [], ["sort-measurements.json"]),
    ("measure_td_array.py", [], ["td-array-measurements.json"]),
    ("measure_writer32.py", [], ["writer32-measurements.json"]),
    ("measure_cache_invalidation.py", [], ["cache-invalidation-measurements.json"]),
    ("measure_discardable_pool.py", [], ["discardable-pool-measurements.json"]),
    ("measure_synchronization.py", [], ["synchronization-measurements.json"]),
]


def main(arguments: list[str]) -> int:
    output = pathlib.Path("build/recorded-workloads")
    isa_trees: list[str] = []
    index = 0
    while index < len(arguments):
        if arguments[index] == "--output" and index + 1 < len(arguments):
            output = pathlib.Path(arguments[index + 1])
            index += 2
        elif arguments[index] == "--isa" and index + 1 < len(arguments):
            isa_trees.append(arguments[index + 1])
            index += 2
        else:
            print(__doc__.strip(), file=sys.stderr)
            return 2
    if not pathlib.Path("build/baseline").is_dir() or not pathlib.Path("build/candidate").is_dir():
        print("Run from stx_skia after building build/baseline and build/candidate", file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    commands = [(f"{tool} {' '.join(tool_arguments)}".strip(),
                 [sys.executable, "-B", f"tools/{tool}"] + tool_arguments, outputs)
                for tool, tool_arguments, outputs in TOOLS]
    commands.append(("measure_isa.py",
                     [sys.executable, "-B", "tools/measure_isa.py", "--json", str(output / "isa-timings.json"),
                      "build/candidate"] + isa_trees, []))
    summary = [f"# Recorded workloads on {platform.node() or 'this host'}", "",
               f"- System: {platform.platform()}, {platform.machine()}",
               f"- Python: {platform.python_version()}", "",
               "| Tool | Result | Seconds |", "|---|---|---|"]
    failures = 0
    for name, command, outputs in commands:
        log_name = name.replace(" ", "").replace("--", "-").replace(".py", "") + ".txt"
        start = time.monotonic()
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        seconds = time.monotonic() - start
        (output / log_name).write_text(completed.stdout, encoding="utf-8")
        for produced in outputs:
            source = pathlib.Path("build") / produced
            if source.is_file():
                shutil.copyfile(source, output / produced.replace("/", "-"))
        result = "ok" if completed.returncode == 0 else f"FAIL ({completed.returncode})"
        if completed.returncode != 0:
            failures += 1
        print(f"{result} {name} in {seconds:.0f} s", flush=True)
        summary.append(f"| {name} | {result} | {seconds:.0f} |")
    (output / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    if failures != 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
