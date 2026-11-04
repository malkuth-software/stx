#!/usr/bin/env python3
"""Pinned libpng sample comparison and bounded sanitizer fuzz smoke experiment."""
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


def verify(root: Path, manifest: Path) -> dict:
    record = json.loads(manifest.read_text())
    for name, expected in record['files'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError('oracle digest mismatch: ' + name)
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--libpng', type=Path, required=True)
    parser.add_argument('--zlib', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('--seconds must be positive')
    png = args.libpng.resolve()
    zlib = args.zlib.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    png_pin = verify(png, ROOT / 'stx_png/tests/libpng_oracle_sha256.json')
    zlib_pin = verify(zlib, ROOT / 'stx_zlib/tests/oracle_sha256.json')
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    env.pop('LSAN_OPTIONS', None)
    versions = {}
    for compiler in ('clang', 'clang++'):
        versions[compiler] = subprocess.check_output([compiler, '--version'], text=True)
        if 'clang version 22.1.8' not in versions[compiler]:
            raise RuntimeError('LLVM 22.1.8 required')
    sources = ['stx_png/include/stx_png/decoder.hpp', 'stx_png/src/decoder.cpp',
               'stx_png/src/encoder.cpp', 'stx_zlib/include/stx_zlib/inflate.hpp',
               'stx_zlib/src/inflate.cpp', 'stx_png/tests/png_decoder_tests.cpp',
               'stx_png/tests/png_oracle.c', 'stx_png/tests/png_dynamic_fixture.hpp',
               'stx_png/fuzz/png_decode_fuzz.cpp', 'tools/validate_png.py']
    record = {'host': platform.platform(), 'machine': platform.machine(), 'compilers': versions,
              'libpng_revision': png_pin['revision'], 'zlib_revision': zlib_pin['revision'],
              'sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
              'commands': [], 'status': 'incomplete'}
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
             '-fno-omit-frame-pointer']
    includes = ['-I' + str(output), '-I' + str(png), '-I' + str(zlib), '-I' + str(ROOT / 'stx_zlib/include'), '-I' + str(ROOT / 'stx_png/include')]
    shutil.copyfile(png / 'scripts/pnglibconf.h.prebuilt', output / 'pnglibconf.h')
    start = time.monotonic()
    try:
        objects = []
        for root, names in ((zlib, ['adler32','crc32','inflate','inffast','inftrees','zutil','deflate','trees']),
                            (png, ['png','pngerror','pngget','pngmem','pngpread','pngread','pngrio',
                                   'pngrtran','pngrutil','pngset','pngtrans','pngwio','pngwrite','pngwtran','pngwutil'])):
            for name in names:
                obj = output / (name + '.o')
                command = ['clang', *flags, *includes, '-DPNG_ARM_NEON_OPT=0', '-c', str(root / (name + '.c')), '-o', str(obj)]
                record['commands'].append(command)
                subprocess.run(command, cwd=output, env=env, check=True)
                objects.append(str(obj))
        oracle = output / 'oracle.o'
        command = ['clang', *flags, *includes, '-c', str(ROOT / 'stx_png/tests/png_oracle.c'), '-o', str(oracle)]
        record['commands'].append(command)
        subprocess.run(command, cwd=output, env=env, check=True)
        candidate = [str(ROOT / name) for name in ('stx_png/src/decoder.cpp',
                     'stx_png/src/encoder.cpp', 'stx_zlib/src/inflate.cpp',
                     'stx_zlib/src/deflate.cpp', 'stx_zlib/src/checksum.cpp')]
        test = output / 'png-tests'
        command = ['clang++', '-std=c++20', *flags, *includes, '-Wall', '-Wextra', '-Wpedantic',
                   '-DSTX_PNG_ORACLE=1', *candidate, str(ROOT / 'stx_png/tests/png_decoder_tests.cpp'),
                   str(oracle), *objects, '-o', str(test)]
        record['commands'].append(command)
        subprocess.run(command, cwd=output, env=env, check=True)
        corpus = output / 'corpus'
        command = [str(test), str(corpus)]
        record['commands'].append(command)
        with (output / 'tests.log').open('w') as log:
            subprocess.run(command, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        record['initial_corpus_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                            for p in sorted(corpus.iterdir()) if p.is_file()}
        fuzz = output / 'png-fuzz'
        command = ['clang++', '-std=c++20', *flags, *includes, '-fsanitize=fuzzer', *candidate,
                   str(ROOT / 'stx_png/fuzz/png_decode_fuzz.cpp'), '-o', str(fuzz)]
        record['commands'].append(command)
        subprocess.run(command, cwd=output, env=env, check=True)
        # Same LLVM RSS-monitor leak workaround as validate_inflate.py; leak
        # detection remains enabled, without suppressions. Not a 24h campaign.
        command = [str(fuzz), '-seed=20261019', '-max_total_time=' + str(args.seconds),
                   '-max_len=65536', '-rss_limit_mb=0', '-malloc_limit_mb=0',
                   '-print_final_stats=1', str(corpus)]
        record['commands'].append(command)
        fuzz_start = time.monotonic()
        with (output / 'fuzz.log').open('w') as log:
            subprocess.run(command, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        record['fuzz_seconds'] = time.monotonic() - fuzz_start
        record['status'] = 'passed'
    finally:
        record['total_seconds'] = time.monotonic() - start
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
    print('PNG oracle matrix and fuzz smoke passed:', output)


if __name__ == '__main__':
    main()
