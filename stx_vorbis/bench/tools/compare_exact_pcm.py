"""Compare complete decoded float bytes against a preserved baseline executable."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def decode(executable: Path, source: Path, destination: Path) -> bytes:
    command: list[str] = [str(executable), str(source), str(destination)]
    subprocess.run(command, check=True, capture_output=True)
    data: bytes = destination.read_bytes()
    return data


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--input', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    baseline: Path = arguments.baseline.resolve()
    candidate: Path = arguments.candidate.resolve()
    output: Path = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, str | int]] = []
    source: Path
    for source in arguments.input:
        before: bytes = decode(baseline, source.resolve(), output / 'baseline.f32')
        after: bytes = decode(candidate, source.resolve(), output / 'candidate.f32')
        if before != after:
            raise RuntimeError('Decoded PCM differs: ' + str(source))
        result: dict[str, str | int] = {
            'file': str(source), 'pcm_bytes': len(after),
            'pcm_sha256': hashlib.sha256(after).hexdigest(),
        }
        results.append(result)
    report: dict[str, object] = {
        'baseline_executable_sha256': hashlib.sha256(baseline.read_bytes()).hexdigest(),
        'candidate_executable_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
        'cases': results,
    }
    (output / 'exact-pcm.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
