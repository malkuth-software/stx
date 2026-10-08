"""Compare every float from the shared benchmark inputs, outside timing."""
import array
import json
import math
from pathlib import Path
import subprocess


def read_floats(path: Path) -> array.array:
    values: array.array = array.array('f')
    data: bytes = path.read_bytes()
    values.frombytes(data)
    return values


def main() -> None:
    root: Path = Path.cwd()
    output: Path = root / '.build/stx-pcm-check'
    output.mkdir(exist_ok=True)
    reference: Path = root / '.build/stx-perf/stx_vorbis_reference.exe'
    decoders: dict[str, Path] = {
        'stx': root / '.build/stx-perf/stx_vorbis_decode.exe',
        'stb': root / '.build/stx-shadow/stb_reference.exe',
    }
    results: list[dict[str, object]] = []
    case: str
    for case in ('stereo-10s', 'surround-10s'):
        source: Path = root / 'stx_vorbis/bench/corpus' / (case + '.ogg')
        expected_path: Path = output / (case + '.reference.f32')
        subprocess.run([str(reference), 'decode', str(source), str(expected_path)], check=True)
        expected: array.array = read_floats(expected_path)
        name: str
        for name in decoders:
            actual_path: Path = output / (case + '.' + name + '.f32')
            subprocess.run([str(decoders[name]), str(source), str(actual_path)], check=True)
            actual: array.array = read_floats(actual_path)
            if len(actual) != len(expected):
                raise RuntimeError('PCM count mismatch: ' + case + '/' + name)
            maximum: float = 0.0
            outside: int = 0
            index: int
            for index in range(len(actual)):
                if not math.isfinite(actual[index]) or not math.isfinite(expected[index]):
                    raise RuntimeError('Nonfinite PCM: ' + case + '/' + name)
                error: float = abs(actual[index] - expected[index])
                maximum = max(maximum, error)
                if error > 2e-6 + 2e-6 * abs(expected[index]):
                    outside += 1
            result: dict[str, object] = {
                'case': case, 'decoder': name, 'samples': len(actual),
                'reference_samples': len(expected), 'maximum_absolute_error': maximum,
                'outside_tolerance': outside,
            }
            results.append(result)
            if name == 'stx' and outside != 0:
                raise RuntimeError('Candidate PCM exceeds tolerance: ' + case)
    (output / 'pcm-correctness.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
