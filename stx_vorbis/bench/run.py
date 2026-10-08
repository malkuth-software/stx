#!/usr/bin/env python3
"""Record raw memory-decode timings and a machine-readable, hash-bound summary."""
import argparse
import csv
import hashlib
import io
import json
import math
import pathlib
import platform
import statistics
import subprocess
import sys


def command_text(command: list[str]) -> str:
    completed: subprocess.CompletedProcess[str] = subprocess.run(command, text=True, capture_output=True, check=True)
    return completed.stdout.strip()


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--input', type=pathlib.Path, nargs='+', required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--modes', nargs='+', default=['scalar', 'automatic'],
                        choices=['scalar', 'automatic', 'neon', 'sse2', 'avx2'])
    parser.add_argument('--iterations', type=int, default=21)
    parser.add_argument('--warmups', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--label', required=True)
    parser.add_argument('--decoder-revision', help='Production revision if binaries use an older source tree')
    arguments: argparse.Namespace = parser.parse_args()
    if min(arguments.iterations, arguments.warmups, arguments.rounds) < 1:
        parser.error('iteration, warmup and round counts must be positive')
    build: pathlib.Path = arguments.build.resolve()
    output: pathlib.Path = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix: str = '.exe' if sys.platform == 'win32' else ''
    executables: list[tuple[str, pathlib.Path, str]] = []
    mode: str = ''
    for mode in arguments.modes:
        executables.append(('stx-' + mode, build / 'bench' / ('stx_bench' + suffix), mode))
    name: str = ''
    missing: list[str] = []
    for name in ('libvorbis', 'tremor', 'stb'):
        executable: pathlib.Path = build / 'bench' / ('stx_bench_' + name + suffix)
        if executable.exists():
            executables.append((name, executable, 'automatic'))
        else:
            missing.append(name)
    report: dict = {
        'label': arguments.label,
        'harness_revision': command_text(['git', 'rev-parse', 'HEAD']),
        'decoder_revision': arguments.decoder_revision or command_text(['git', 'rev-parse', 'HEAD']),
        'working_tree': command_text(['git', 'status', '--short']),
        'platform': platform.platform(), 'machine': platform.machine(),
        'compiler': command_text([arguments.compiler, '--version']),
        'iterations': arguments.iterations, 'warmups': arguments.warmups, 'rounds': arguments.rounds,
        'missing_oracles': missing, 'cases': [], 'binary_sha256': {},
        'timing_scope': 'memory input; constructor through EOF; cleanup excluded; first PCM includes setup and first audio; sparse output probes; warm process',
    }
    for name, executable, mode in executables:
        report['binary_sha256'][name] = hashlib.sha256(executable.read_bytes()).hexdigest()
    source_argument: pathlib.Path
    for source_argument in arguments.input:
        source: pathlib.Path = source_argument.resolve()
        case: dict = {'name': source.name, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'results': {}}
        collected: dict[str, list[dict[str, str]]] = {}
        failures: dict[str, str] = {}
        round_index: int = 0
        for round_index in range(arguments.rounds):
            # Rotate implementation order to reduce monotonic temperature/order bias.
            entry_index: int = 0
            for entry_index in range(len(executables)):
                entry: tuple[str, pathlib.Path, str] = executables[(entry_index + round_index) % len(executables)]
                name, executable, mode = entry
                command: list[str] = [str(executable), str(source), str(arguments.iterations), str(arguments.warmups), mode]
                completed: subprocess.CompletedProcess[str] = subprocess.run(command, text=True, capture_output=True)
                stem: str = source.name + '.' + name + '.' + str(round_index)
                (output / (stem + '.csv')).write_text(completed.stdout)
                if completed.returncode != 0:
                    failures[name] = completed.stderr.strip()
                    (output / (stem + '.stderr')).write_text(completed.stderr)
                    continue
                rows: list[dict[str, str]] = list(csv.DictReader(io.StringIO(completed.stdout)))
                if len(rows) != arguments.iterations:
                    raise RuntimeError('wrong number of timing rows: ' + stem)
                collected.setdefault(name, []).extend(rows)
        reference_samples: int = 0
        if 'libvorbis' in collected:
            reference_samples = int(collected['libvorbis'][0]['samples'])
        for name in collected:
            rows = collected[name]
            result: dict = {'runs': len(rows), 'frames': int(rows[0]['frames']), 'samples': int(rows[0]['samples'])}
            result['sample_count_matches_libvorbis'] = None if reference_samples == 0 else result['samples'] == reference_samples
            for field in ('first_pcm_ms', 'remaining_ms', 'total_ms'):
                values: list[float] = []
                row: dict[str, str]
                for row in rows:
                    value: float = float(row[field])
                    if not math.isfinite(value) or value < 0:
                        raise RuntimeError('invalid timing: ' + name)
                    values.append(value)
                values.sort()
                result[field] = {'median': statistics.median(values), 'minimum': values[0],
                                 'p95': values[math.ceil(0.95 * len(values)) - 1]}
            case['results'][name] = result
        case['failures'] = failures
        report['cases'].append(case)
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
