#!/usr/bin/env python3
"""Reproduce the owned-inflate baseline, candidate and vectorization ablation.

No fetching and no production dependency changes. The pinned upstream zlib
oracle supplies fixtures and a separately timed comparison. This is an isolated
whole-buffer synthetic benchmark, not the full performance acceptance matrix.

The baseline is GUI.Forms' owned inflate at 123f13cd, before it became
stx_zlib: pass a GUI.Forms clone with --gui-forms, or an export with --baseline.
The benchmark source (tools/codec_bench/inflate_benchmark.cpp) is written
against GUI.Forms' gui_forms::compression names: the baseline's own header for
the baseline, tools/codec_bench/seam (stx_zlib under those names) otherwise.
"""
from __future__ import annotations

import argparse
import atexit
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = '123f13cd'
BASELINE_SHA256 = {'include/gui_forms/compression/inflate.hpp': 'ff1417a0e2f5a7bc7d484d95d84085af0f6a8d3371d95cbe8820eaf5240223f4', 'src/core/compression/inflate.cpp': 'f711a562090564846e186e81066972145ca2a528841f90c903af18a05c53a590'}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inspect-x64', action='store_true', help='emit SSE2-only assembly on macOS; does not execute it')
    parser.add_argument('--baseline', type=Path, help='exported baseline (the two files of BASELINE_SHA256)')
    parser.add_argument('--gui-forms', type=Path, help='GUI.Forms clone to read the baseline revision from')
    args = parser.parse_args()
    if (args.baseline is None) == (args.gui_forms is None):
        parser.error('give exactly one of --baseline and --gui-forms')
    oracle = args.oracle.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'stx_zlib/tests/oracle_sha256.json').read_text())
    for name, expected in manifest['files'].items():
        if digest(oracle / name) != expected:
            raise RuntimeError('oracle source digest mismatch: ' + name)
    compilers = {name: subprocess.check_output([name, '--version'], text=True)
                 for name in ('clang', 'clang++')}
    if any('clang version 22.1.8' not in value for value in compilers.values()):
        raise RuntimeError('LLVM 22.1.8 required')
    commands: list[list[str]] = []

    def run(command: list[str], log: str | None = None) -> None:
        commands.append(command)
        print(' '.join(command), flush=True)
        if log:
            with (output / log).open('w') as stream:
                subprocess.run(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT, check=True)
        else:
            subprocess.run(command, cwd=output, check=True)

    source_hashes = {name: digest(ROOT / name) for name in
                     ('tools/codec_bench/seam/gui_forms/compression/inflate.hpp',
                      'stx_zlib/include/stx_zlib/inflate.hpp', 'stx_zlib/src/inflate.cpp',
                      'tools/codec_bench/inflate_benchmark.cpp', 'tools/benchmark_inflate.py')}
    hardware = ''
    if platform.system() == 'Darwin':
        hardware = subprocess.check_output(['/usr/sbin/sysctl', 'machdep.cpu.brand_string', 'hw.memsize'], text=True)

    def save_record() -> None:
        corpus = {path.name: digest(path) for path in sorted(output.iterdir())
                  if path.suffix in ('.deflate', '.raw')}
        record = {'host': platform.platform(), 'machine': platform.machine(), 'hardware': hardware,
                  'compilers': compilers, 'commands': commands, 'baseline': BASELINE,
                  'oracle_revision': manifest['revision'], 'source_sha256': source_hashes,
                  'corpus_sha256': corpus, 'flags': ['-O3', '-DNDEBUG'], 'architecture_flags': [], 'lto': False,
                  'samples': 'calibrate to 50 ms, two warmup batches, five timed batches; reset included'}
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')

    atexit.register(save_record)

    baseline = output / 'baseline-source'
    files = ('include/gui_forms/compression/inflate.hpp', 'src/core/compression/inflate.cpp')
    for name in files:
        destination = baseline / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if args.baseline:
            content = (args.baseline.resolve() / name).read_bytes()
        else:
            content = subprocess.check_output(['git', '-C', str(args.gui_forms.resolve()), 'show',
                                               BASELINE + ':' + name])
        if hashlib.sha256(content).hexdigest() != BASELINE_SHA256[name]:
            raise RuntimeError('baseline source digest mismatch: ' + name)
        destination.write_bytes(content)
    flags = ['-O3', '-DNDEBUG']
    objects: list[str] = []
    for source in ('adler32', 'crc32', 'deflate', 'inflate', 'inffast', 'inftrees', 'trees', 'zutil'):
        target = source + '.o'
        run(['clang', *flags, '-I' + str(oracle), '-c', str(oracle / (source + '.c')), '-o', target])
        objects.append(target)
    for variant in ('baseline', 'candidate', 'no-vector'):
        source_root = baseline if variant == 'baseline' else ROOT
        extra = ['-fno-vectorize', '-fno-slp-vectorize'] if variant == 'no-vector' else []
        common = ['clang++', '-std=c++20', *flags, *extra, '-Wall', '-Wextra', '-Wpedantic',
                  '-I' + str(baseline / 'include' if variant == 'baseline' else ROOT / 'tools/codec_bench/seam'),
                  '-I' + str(source_root / 'stx_zlib/include'), '-I' + str(oracle)]
        source = str(source_root / ('src/core/compression/inflate.cpp' if variant == 'baseline'
                                    else 'stx_zlib/src/inflate.cpp'))
        # The candidate's Adler-32 lives in stx_zlib/src/checksum.cpp; the
        # baseline revision predates that file.
        checksum = [] if variant == 'baseline' else [str(source_root / 'stx_zlib/src/checksum.cpp')]
        run([*common, '-Rpass=loop-vectorize', '-Rpass-missed=loop-vectorize', source, *checksum,
             str(ROOT / 'tools/codec_bench/inflate_benchmark.cpp'), *objects, '-o', variant], variant + '-compiler.log')
        run([*common, '-S', source, '-o', variant + '.s'])
        run([*common, '-fstack-usage', '-c', source, '-o', variant + '-stack.o'])
        run([str(output / variant)], variant + '.csv')
    if args.inspect_x64:
        sdk = subprocess.check_output(['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
        run(['clang++', '--target=x86_64-apple-macos14', '-std=c++20', *flags,
             '-isysroot', sdk, '-march=x86-64', '-mtune=generic', '-msse2', '-mno-avx', '-Rpass=loop-vectorize', '-Rpass-missed=loop-vectorize',
             '-I' + str(ROOT / 'stx_zlib/include'),
             '-S', str(ROOT / 'stx_zlib/src/inflate.cpp'),
             '-o', 'candidate-sse2.s'], 'candidate-sse2-compiler.log')



if __name__ == '__main__':
    main()
