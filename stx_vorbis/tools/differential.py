#!/usr/bin/env python3
"""Reproducible libvorbis differential corpus, including chains and format changes."""
import argparse
import array
import json
import pathlib
import subprocess
import sys


def run(command: list[str]) -> None:
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def samples(path: pathlib.Path) -> array.array:
    result: array.array = array.array('f')
    result.frombytes(path.read_bytes())
    return result


def compare(decoder: pathlib.Path, reference: pathlib.Path, source: pathlib.Path,
            work: pathlib.Path) -> dict[str, str | int | float]:
    actual_path: pathlib.Path = work / 'actual.f32'
    expected_path: pathlib.Path = work / 'expected.f32'
    run([str(decoder), str(source), str(actual_path)])
    run([str(reference), 'decode', str(source), str(expected_path)])
    actual: array.array = samples(actual_path)
    expected: array.array = samples(expected_path)
    if len(actual) != len(expected):
        raise RuntimeError(f'{source.name}: sample counts {len(actual)} != {len(expected)}')
    maximum: float = 0.0
    squared: float = 0.0
    index: int = 0
    for index in range(len(actual)):
        error: float = abs(actual[index] - expected[index])
        maximum = max(maximum, error)
        squared += error * error
        tolerance: float = 2e-6 + 2e-6 * abs(expected[index])
        if error > tolerance:
            raise RuntimeError(f'{source.name}: sample {index}, error {error}, tolerance {tolerance}')
    rms: float = (squared / max(len(actual), 1)) ** 0.5
    return {'file': source.name, 'samples': len(actual), 'maximum_absolute_error': maximum, 'rms_error': rms}


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--work', type=pathlib.Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    work: pathlib.Path = arguments.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    suffix: str = '.exe' if sys.platform == 'win32' else ''
    decoder: pathlib.Path = arguments.build.resolve() / ('stx_vorbis_decode' + suffix)
    reference: pathlib.Path = arguments.build.resolve() / ('stx_vorbis_reference' + suffix)
    streaming: pathlib.Path = arguments.build.resolve() / ('stx_vorbis_streaming' + suffix)
    rows: list[dict[str, str | int | float]] = []
    paths: list[pathlib.Path] = []
    cases: tuple[tuple[int, int, int, float], ...] = (
        (1, 44100, 44117, 0.1), (1, 8000, 17, 0.0), (2, 48000, 96017, 0.5),
        (6, 48000, 48017, 0.4), (8, 96000, 17017, 0.8), (2, 22050, 40000, -0.1))
    channels: int = 0
    rate: int = 0
    frames: int = 0
    quality: float = 0.0
    for channels, rate, frames, quality in cases:
        path: pathlib.Path = work / f'c{channels}-r{rate}-n{frames}.ogg'
        run([str(reference), 'encode', str(path), str(channels), str(rate), str(frames), str(quality)])
        paths.append(path)
    fixture_directory: pathlib.Path = pathlib.Path(__file__).resolve().parent.parent / 'tests' / 'corpus'
    paths.extend(sorted(fixture_directory.glob('*.ogg')))
    chain: pathlib.Path = work / 'chained.ogg'
    data: bytearray = bytearray()
    path: pathlib.Path
    for path in paths:
        rows.append(compare(decoder, reference, path, work))
        data.extend(path.read_bytes())
    chain.write_bytes(data)
    rows.append(compare(decoder, reference, chain, work))
    command: list[str] = [str(streaming)]
    for path in paths:
        command.append(str(path))
    run(command)
    output: str = json.dumps(rows, indent=2) + '\n'
    (work / 'differential-results.json').write_text(output, encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
