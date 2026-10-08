"""Full PCM checks for production BODFT, including finite-value validation."""
import argparse
import array
import json
import math
from pathlib import Path
import subprocess
import sys


def decode(command: list[str], destination: Path) -> array.array:
    subprocess.run(command, check=True, capture_output=True)
    samples: array.array = array.array('f')
    samples.frombytes(destination.read_bytes())
    return samples


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--baseline-scalar', action='store_true')
    parser.add_argument('--input', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    build: Path = arguments.build.resolve()
    output: Path = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix: str = '.exe' if sys.platform == 'win32' else ''
    candidate: Path = build / ('stx_vorbis_decode' + suffix)
    baseline: Path = arguments.baseline.resolve()
    oracle: Path = build / ('stx_vorbis_reference' + suffix)
    rows: list[dict[str, str | int | float]] = []
    source: Path
    for source in arguments.input:
        source = source.resolve()
        actual_path: Path = output / 'candidate.f32'
        before_path: Path = output / 'baseline.f32'
        oracle_path: Path = output / 'oracle.f32'
        actual: array.array = decode([str(candidate), str(source), str(actual_path)], actual_path)
        baseline_command: list[str] = [str(baseline), str(source), str(before_path)]
        if arguments.baseline_scalar:
            baseline_command.append('--scalar')
        before: array.array = decode(baseline_command, before_path)
        reference: array.array = decode([str(oracle), 'decode', str(source), str(oracle_path)], oracle_path)
        if len(actual) != len(before) or len(actual) != len(reference):
            raise RuntimeError('Sample count mismatch: ' + str(source))
        maximum: float = 0.0
        baseline_maximum: float = 0.0
        changed: int = 0
        index: int
        for index in range(len(actual)):
            if not math.isfinite(actual[index]) or not math.isfinite(reference[index]) or not math.isfinite(before[index]):
                raise RuntimeError('Nonfinite PCM: ' + str(source))
            error: float = abs(actual[index] - reference[index])
            if error > 2e-6 + 2e-6 * abs(reference[index]):
                raise RuntimeError('PCM outside oracle tolerance: ' + str(source))
            maximum = max(maximum, error)
            baseline_maximum = max(baseline_maximum, abs(actual[index] - before[index]))
            if abs(actual[index] - before[index]) > 2e-6 + 2e-6 * abs(before[index]):
                raise RuntimeError('PCM outside baseline tolerance: ' + str(source))
            if actual[index] != before[index]:
                changed += 1
        row: dict[str, str | int | float] = {
            'file': source.name, 'samples': len(actual),
            'maximum_oracle_error': maximum, 'maximum_baseline_error': baseline_maximum,
            'values_changed_from_baseline': changed,
            'byte_identical_to_baseline': actual.tobytes() == before.tobytes(),
        }
        rows.append(row)
    (output / 'pcm.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
