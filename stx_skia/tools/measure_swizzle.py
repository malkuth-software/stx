"""Red/blue swap timings and generated-code evidence for stx_skia_swizzle.

Runs <build>/stx_skia_swizzle_tests --benchmark seven times and records the median
seconds per pass of each kernel (stx_skia's mask kernel, the byte-permutation loop GUI.Forms
writes today, Skia's SkSwapRB, and a one-pixel scalar loop) for each case and mode. Every
kernel must leave identical bytes for a case and mode. Then it disassembles the native
object and emits x86-64 SSE2 and SSE4.1 previews from the same source.

The x86-64 previews come from this machine's LLVM with an Apple target. They are not the
pinned Windows toolchain, and they time nothing.
"""
import json
from pathlib import Path
import platform
import shlex
import shutil
import statistics
import subprocess
import sys

TRIALS = 7
BUILD = Path("build/candidate")
OUTPUT = Path("build/swizzle")


def run_once():
    result = subprocess.check_output(
        [str(BUILD / "stx_skia_swizzle_tests"), "--benchmark"], text=True)
    rows = {}
    for line in result.splitlines()[1:]:
        kernel, case, mode, pixels, iterations, seconds, digest = line.split(",")
        rows[(kernel, case, mode)] = {"pixels": int(pixels), "iterations": int(iterations),
                                      "seconds": float(seconds), "hash": digest}
    return rows


def measure():
    trials = [run_once() for _ in range(TRIALS)]
    keys = list(trials[0])
    rows = []
    for key in keys:
        samples = [trial[key]["seconds"] for trial in trials]
        median = statistics.median(samples)
        pixels = trials[0][key]["pixels"]
        rows.append({"kernel": key[0], "case": key[1], "mode": key[2], "pixels": pixels,
                     "iterations": trials[0][key]["iterations"], "seconds": samples,
                     "median_seconds": median,
                     "median_gigapixels_per_second": pixels / median / 1e9,
                     "hash": trials[0][key]["hash"]})
    # Every kernel leaves the same bytes for a case and mode, in every trial.
    for case_mode in {(row["case"], row["mode"]) for row in rows}:
        hashes = {trial[key]["hash"] for trial in trials for key in trial
                  if (key[1], key[2]) == case_mode}
        assert len(hashes) == 1, (case_mode, hashes)
    by_key = {(row["kernel"], row["case"], row["mode"]): row for row in rows}
    for row in rows:
        skia = by_key[("skia", row["case"], row["mode"])]["median_seconds"]
        skia_default = by_key[("skia_default", row["case"], row["mode"])]["median_seconds"]
        scalar = by_key[("scalar", row["case"], row["mode"])]["median_seconds"]
        row["median_over_skia"] = row["median_seconds"] / skia
        row["median_over_skia_default"] = row["median_seconds"] / skia_default
        row["median_over_scalar"] = row["median_seconds"] / scalar
    for row in rows:
        print(f'{row["case"]:>15} {row["mode"]:>8} {row["kernel"]:>12} '
              f'{row["median_seconds"] * 1e6:12.3f} us  '
              f'{row["median_gigapixels_per_second"]:7.3f} Gpx/s  '
              f'vs skia {row["median_over_skia"]:.3f}  '
              f'vs skia_default {row["median_over_skia_default"]:.3f}  '
              f'vs scalar {row["median_over_scalar"]:.3f}  '
              f'range {min(row["seconds"]) * 1e6:.3f}-{max(row["seconds"]) * 1e6:.3f} us')
    compiler = subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0]
    record = {"platform": platform.platform(), "machine": platform.machine(),
              "processor": platform.processor(),
              "compiler": compiler, "trials": TRIALS, "measurements": rows,
              "scope": "Seven runs of one process each; per case and mode, one warm-up pass, "
                       "then about 256 MiB of pixels in repeated passes over the same buffers, "
                       "timed with steady_clock. Rows of the 300x200 case are 4096 bytes "
                       "apart; other cases are contiguous rows. Allocation excluded. Steady "
                       "state with warm caches for the small cases; not application latency."}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "measurements.json").write_text(json.dumps(record, indent=2) + "\n")


def compile_entry(source):
    commands = json.loads((BUILD / "compile_commands.json").read_text())
    entry = next(row for row in commands if row["file"].endswith(source))
    args = shlex.split(entry["command"])
    output = args.index("-o")
    del args[output:output + 2]
    args.remove("-c")
    return entry["directory"], args


def emit_code():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    directory, args = compile_entry("src/swizzle.cpp")
    native = args + ["-S", "-o", str((OUTPUT / "swizzle-native.s").resolve())]
    subprocess.run(native, cwd=directory, check=True)
    objdump = shutil.which("llvm-objdump") or "objdump"
    test = subprocess.check_output(
        [objdump, "-d", "--no-show-raw-insn", "-C", str(BUILD / "stx_skia_swizzle_tests")],
        text=True)
    (OUTPUT / "swizzle-tests-native.txt").write_text(test)
    if shutil.which("xcrun") is None:
        print("code written to", OUTPUT, "(no Apple SDK: x86-64 previews skipped)")
        return
    sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"],
                                  text=True).strip()
    x86 = ["--target=x86_64-apple-macos14", "-isysroot", sdk, "-march=x86-64",
           "-mtune=generic"]
    sse2 = args + x86 + ["-mno-sse3", "-mno-ssse3", "-mno-sse4.1", "-S", "-o",
                         str((OUTPUT / "swizzle-x86_64-sse2.s").resolve())]
    subprocess.run(sse2, cwd=directory, check=True)
    sse41 = args + x86 + ["-msse4.1", "-S", "-o",
                          str((OUTPUT / "swizzle-x86_64-sse41.s").resolve())]
    subprocess.run(sse41, cwd=directory, check=True)
    print("code written to", OUTPUT)


if __name__ == "__main__":
    arguments = sys.argv[1:]
    if len(arguments) >= 2 and arguments[0] == "--build":
        BUILD = Path(arguments[1])
        OUTPUT = BUILD / "swizzle"
        arguments = arguments[2:]
    if arguments == ["--code-only"]:
        emit_code()
    elif arguments == ["--no-code"]:
        measure()
    else:
        measure()
        emit_code()
