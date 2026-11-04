#!/usr/bin/env python3
"""Automated memory integrity review; report only PASS or STOP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
REVIEW_OUTPUT = None


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    global REVIEW_OUTPUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('--seconds must be positive')
    oracle = args.oracle.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    REVIEW_OUTPUT = output
    manifest = json.loads((ROOT / 'stx_zlib/tests/oracle_sha256.json').read_text())
    for name, expected in manifest['files'].items():
        if digest(oracle / name) != expected:
            raise RuntimeError('oracle digest mismatch: ' + name)
    versions = {}
    for compiler in ('clang', 'clang++'):
        versions[compiler] = subprocess.check_output([compiler, '--version'], stderr=subprocess.PIPE, text=True)
        if 'clang version 22.1.8' not in versions[compiler]:
            raise RuntimeError('LLVM 22.1.8 required')
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    environment.pop('LSAN_OPTIONS', None)
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
             '-fno-omit-frame-pointer', '-fsanitize=fuzzer-no-link']
    cxx_flags = ['-std=c++20']
    link_flags = []
    if platform.system() == 'Darwin':
        runtime = Path(environment['GUI_FORMS_LLVM_RUNTIME'])
        cxx_flags += ['-stdlib=libc++', '-nostdinc++', '-isystem', str(runtime / 'include/c++/v1')]
        link_flags += ['-L' + str(runtime / 'lib'), '-Wl,-rpath,' + str(runtime / 'lib'), '-lunwind']
    commands = []
    sources = ['stx_zlib/src/deflate.cpp', 'stx_zlib/include/stx_zlib/deflate.hpp',
               'stx_zlib/include/stx_zlib/inflate.hpp', 'stx_zlib/tests/deflate_memory_review.cpp',
               'tools/review_deflate_memory.py']
    hashes = {}
    for name in sources:
        hashes[name] = digest(ROOT / name)
        destination = output / 'source' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    record = {'source_sha256': hashes, 'host': platform.platform(), 'machine': platform.machine(),
              'compilers': versions, 'oracle_revision': manifest['revision'], 'commands': commands}
    try:
        objects = []
        for name in ('adler32', 'crc32', 'inflate', 'inffast', 'inftrees', 'zutil'):
            target = str(output / (name + '.o'))
            command = ['clang', *flags, '-I' + str(oracle), '-c', str(oracle / (name + '.c')), '-o', target]
            commands.append(command)
            with (output / 'compiler.log').open('a') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, env=environment)
            objects.append(target)
        executable = output / ('deflate-memory-review.exe' if os.name == 'nt' else 'deflate-memory-review')
        command = ['clang++', *cxx_flags, *flags, '-fsanitize=fuzzer', '-Wall', '-Wextra', '-Wpedantic',
                   '-I' + str(ROOT / 'stx_zlib/include'), '-I' + str(oracle),
                   str(ROOT / 'stx_zlib/src/deflate.cpp'), str(ROOT / 'stx_zlib/src/checksum.cpp'),
                   str(ROOT / 'stx_zlib/tests/deflate_memory_review.cpp'),
                   *objects, *link_flags, '-o', str(executable)]
        commands.append(command)
        with (output / 'compiler.log').open('a') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, env=environment)
        corpus = output / 'corpus'
        corpus.mkdir(exist_ok=True)
        crashes = output / 'crashes'
        crashes.mkdir(exist_ok=True)
        seed_hashes = {}
        for level in (0, 1, 6, 9):
            for count in (0, 1, 257, 32769, 65536):
                payload = bytearray(count)
                for index in range(count):
                    payload[index] = (index * 13 + index // 137) & 255
                seed = bytes((level, 0, 7, 7, 2, 0, 0, 127)) + payload
                path = corpus / f'level-{level}-bytes-{count}'
                if not path.exists():
                    path.write_bytes(seed)
                seed_hashes[path.name] = digest(path)
        record['initial_seed_sha256'] = seed_hashes
        command = [str(executable), '-seed=20261020', '-max_total_time=' + str(args.seconds),
                   '-max_len=65544', '-timeout=30', '-print_final_stats=1', '-rss_limit_mb=0',
                   '-malloc_limit_mb=0', '-artifact_prefix=' + str(crashes) + '/', str(corpus)]
        commands.append(command)
        start = time.monotonic()
        with (output / 'diagnostics.log').open('w') as log:
            completed = subprocess.run(command, cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT)
        record['seconds'] = time.monotonic() - start
        record['exit_code'] = completed.returncode
        record['corpus_files'] = sum(1 for path in corpus.iterdir() if path.is_file())
        completed.check_returncode()
    finally:
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    try:
        main()
    except (Exception, KeyboardInterrupt):
        if REVIEW_OUTPUT is not None:
            (REVIEW_OUTPUT / 'stop.log').write_text(traceback.format_exc())
        print('STOP')
        sys.exit(1)
    print('PASS')
