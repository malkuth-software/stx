#!/usr/bin/env python3
"""Isolated phase-2 experiment; supplements, never replaces, the full CI suite.

Uses a caller-provided pinned zlib source tree solely as a test oracle. Does not
fetch dependencies or change the shipped build. Outputs binaries, logs and the
mutated corpus under the requested build directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str], output: Path, environment: dict[str, str]) -> None:
    print(' '.join(command), flush=True)
    subprocess.run(command, cwd=output, env=environment, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('--seconds must be positive')
    oracle = args.oracle.resolve()
    output = args.output.resolve()
    manifest_path = ROOT / 'stx_zlib/tests/oracle_sha256.json'
    manifest = json.loads(manifest_path.read_text())
    for name, expected in manifest['files'].items():
        actual = hashlib.sha256((oracle / name).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError('oracle source digest mismatch: ' + name)
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    # No project or inherited leak suppressions in this isolated experiment.
    environment.pop('LSAN_OPTIONS', None)
    cc = environment.get('CC', 'clang')
    cxx = environment.get('CXX', 'clang++')
    versions: dict[str, str] = {}
    for compiler in (cc, cxx):
        version = subprocess.check_output([compiler, '--version'], text=True)
        if 'clang version 22.1.8' not in version:
            raise RuntimeError('LLVM 22.1.8 required: ' + compiler)
        versions[compiler] = version
    source_hashes: dict[str, str] = {}
    for name in ('stx_zlib/src/inflate.cpp', 'stx_zlib/include/stx_zlib/inflate.hpp',
                 'stx_zlib/tests/inflate_tests.cpp', 'stx_zlib/tests/inflate_fixture.hpp',
                 'stx_zlib/fuzz/inflate_fuzz.cpp'):
        source_hashes[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    common = ['-O1', '-g', '-fsanitize=address,undefined',
              '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer']
    objects: list[str] = []
    for source in ('adler32', 'crc32', 'deflate', 'inflate', 'inffast', 'inftrees', 'trees', 'zutil'):
        command = [cc, *common, '-I' + str(oracle)]
        if args.strict:
            command.append('-DINFLATE_STRICT')
        command.extend(['-c', str(oracle / (source + '.c')), '-o', source + '.o'])
        run(command, output, environment)
        objects.append(source + '.o')
    suffix = '.exe' if os.name == 'nt' else ''
    test = output / ('inflate-tests' + suffix)
    fuzz = output / ('inflate-fuzz' + suffix)
    candidate = str(ROOT / 'stx_zlib/src/inflate.cpp')
    arguments = [cxx, '-std=c++20', *common, '-DSTX_ZLIB_INFLATE_ORACLE=1',
                 '-I' + str(ROOT / 'stx_zlib/include'),
                 '-I' + str(oracle), candidate, str(ROOT / 'stx_zlib/src/checksum.cpp')]
    run([*arguments, '-Wall', '-Wextra', '-Wpedantic', '-fstack-usage',
         str(ROOT / 'stx_zlib/tests/inflate_tests.cpp'), *objects, '-o', str(test)], output, environment)
    with (output / 'tests.log').open('w') as log:
        subprocess.run([str(test)], cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    run([*arguments, '-fsanitize=fuzzer', str(ROOT / 'stx_zlib/fuzz/inflate_fuzz.cpp'),
         *objects, '-o', str(fuzz)], output, environment)
    corpus = output / 'corpus'
    corpus.mkdir(exist_ok=True)
    for seed in (ROOT / 'stx_zlib/fuzz/corpus/inflate').iterdir():
        if seed.is_file():
            shutil.copyfile(seed, corpus / seed.name)
    command = [str(fuzz), '-seed=20261019', '-max_total_time=' + str(args.seconds),
               '-max_len=4096', '-print_final_stats=1', '-rss_limit_mb=0',
               '-malloc_limit_mb=0', str(corpus)]
    start = time.monotonic()
    with (output / 'fuzz.log').open('w') as log:
        completed = subprocess.run(command, cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - start
    record = {'source_sha256': source_hashes, 'host': platform.platform(), 'machine': platform.machine(), 'compilers': versions,
              'oracle_revision': manifest['revision'], 'strict': args.strict,
              'seconds': elapsed, 'exit_code': completed.returncode, 'fuzz_command': command}
    (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
    print((output / 'tests.log').read_text(), end='')
    print('\n'.join((output / 'fuzz.log').read_text().splitlines()[-15:]))
    completed.check_returncode()


if __name__ == '__main__':
    main()
