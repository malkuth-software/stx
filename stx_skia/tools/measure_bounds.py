"""Times the collection-bounds failure-path variants built by tools/measure_bounds.sh."""
import json
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time

VARIANTS = ["trap", "unreachable", "abort"]
# (name, executable, arguments). Corpus tests run without an output path, so no file I/O.
WORKLOADS = [
    ("render_draw", "stx_skia_render_tests", []),
    ("path_construction", "stx_skia_path_construction_tests", []),
    ("curve_geometry", "stx_skia_curve_geometry_tests", []),
    ("contour_measure", "stx_skia_contour_measure_tests", []),
    ("stroke_geometry", "stx_skia_stroke_geometry_tests", []),
    ("dash_effects", "stx_skia_dash_effect_tests", []),
    ("td_array_benchmark", "stx_skia_td_array_tests", ["--benchmark"]),
    ("tarray_benchmark", "stx_skia_tarray_construction_tests", ["--benchmark"]),
]
TRIALS = 9


def run(variant, executable, arguments):
    command = [str(Path("build") / f"bounds-{variant}" / executable)] + arguments
    start = time.perf_counter()
    output = subprocess.check_output(command, text=True)
    elapsed = time.perf_counter() - start
    return output, elapsed


def main():
    results = {name: {variant: [] for variant in VARIANTS} for name, _, _ in WORKLOADS}
    outputs = {}
    for name, executable, arguments in WORKLOADS:
        for variant in VARIANTS:
            run(variant, executable, arguments)  # warm-up
        for trial in range(TRIALS):
            order = VARIANTS[trial % 3:] + VARIANTS[:trial % 3]
            for variant in order:
                output, elapsed = run(variant, executable, arguments)
                if name == "render_draw":
                    elapsed = float(re.search(r"draw_seconds=([\d.]+)", output).group(1))
                results[name][variant].append(elapsed)
                hashes = re.findall(r"hash=(\d+)", output)
                if hashes and "benchmark" not in name:
                    outputs.setdefault(name, set()).add(tuple(hashes))
    for name, seen in outputs.items():
        if len(seen) != 1:
            raise RuntimeError(f"{name} output differs between variants")
    Path("build/bounds-measurements.json").write_text(json.dumps({"seconds": results}, indent=2) + "\n")
    sizes = {}
    for variant in VARIANTS:
        library = Path("build") / f"bounds-{variant}" / "libstx_skia.a"
        text = subprocess.check_output(["llvm-size", "--format=berkeley", "--totals", str(library)], text=True)
        total = text.strip().splitlines()[-1].split()
        disassembly = subprocess.check_output(["llvm-objdump", "-d", "--no-show-raw-insn",
                                               str(library)], text=True)
        sizes[variant] = {
            "archive_bytes": library.stat().st_size,
            "text_bytes": int(total[0]),
            "trap_instructions": len(re.findall(r"\tbrk\t", disassembly)),
        }
    report = {"platform": platform.platform(), "machine": platform.machine(),
              "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
              "trials": TRIALS, "seconds": results, "sizes": sizes,
              "limits": "Wall time per process (render: draw time only). Current host only. "
                        "Variant headers also change SkUNREACHABLE's other, cold uses."}
    Path("build/bounds-measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    for name in results:
        medians = {variant: statistics.median(results[name][variant]) for variant in VARIANTS}
        base = medians["trap"]
        print(name, " ".join(f"{v}={medians[v]:.4f}s({(medians[v] / base - 1) * 100:+.1f}%)"
                             for v in VARIANTS))
    for variant, entry in sizes.items():
        print(variant, entry)
    print("PASS")


if __name__ == "__main__":
    main()
