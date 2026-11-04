#!/usr/bin/env python3
"""Compare exact match indexes with identical answers and query schedules."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from benchmark_deflate import png_filtered

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--png', type=Path, action='append', default=[])
    parser.add_argument('--png-bytes', type=int, default=131072,
                        help='maximum extracted filter-byte sample per PNG; recorded explicitly')
    parser.add_argument('--png-offset', type=int, default=0)
    parser.add_argument('--files-only', action='store_true')
    parser.add_argument('--no-sharing', action='store_true')
    parser.add_argument('--radix', action='store_true')
    parser.add_argument('--radix-finger', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--greedy-only', action='store_true')
    parser.add_argument('--random-only', action='store_true')
    parser.add_argument('--random-sweep', action='store_true')
    parser.add_argument('--assembly', action='store_true')
    parser.add_argument('--inline-radix', action='store_true')
    parser.add_argument('--small-radix', action='store_true')
    parser.add_argument('--singleton-radix', action='store_true')
    parser.add_argument('--lazy-radix', action='store_true')
    parser.add_argument('--restrict-index', action='store_true')
    args = parser.parse_args()
    if args.radix_finger:
        args.radix = True
    if args.singleton_radix and not (args.radix_finger and args.small_radix):
        parser.error('--singleton-radix requires --radix-finger and --small-radix')
    if args.lazy_radix and not args.singleton_radix:
        parser.error('--lazy-radix requires --singleton-radix')
    if args.png_bytes < 1 or args.png_bytes > 16 * 1024 * 1024:
        parser.error('--png-bytes must be in 1..16777216')
    if args.png_offset < 0:
        parser.error('--png-offset must be nonnegative')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = subprocess.check_output(['clang++', '--version'], text=True)
    if 'clang version 22.1.8' not in compiler:
        raise RuntimeError('LLVM 22.1.8 required')
    flags = ['-std=c++20', '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow']
    link_flags = []
    if args.no_sharing:
        flags.append('-DSTX_MATCH_INDEX_SHARE_PREFIXES=0')
    if args.radix:
        flags.append('-DSTX_MATCH_INDEX_RADIX=1')
    if args.radix_finger:
        flags.append('-DSTX_MATCH_INDEX_RADIX_FINGER=1')
    if args.inline_radix:
        flags.append('-DSTX_MATCH_INDEX_RADIX_INLINE=1')
    if args.small_radix:
        flags.append('-DSTX_MATCH_INDEX_RADIX_SMALL=1')
    if args.singleton_radix:
        flags.append('-DSTX_MATCH_INDEX_RADIX_SINGLETON=1')
    if args.lazy_radix:
        flags.append('-DSTX_MATCH_INDEX_RADIX_LAZY=1')
    if args.restrict_index:
        flags.append('-DSTX_MATCH_INDEX_RESTRICT=1')
    if platform.system() == 'Darwin':
        runtime = Path(os.environ['GUI_FORMS_LLVM_RUNTIME'])
        flags += ['-stdlib=libc++', '-nostdinc++', '-isystem', str(runtime / 'include/c++/v1')]
        link_flags = ['-L' + str(runtime / 'lib'), '-Wl,-rpath,' + str(runtime / 'lib'), '-lunwind']
    sources = ['stx_zlib/bench/match_index.cpp', 'tools/benchmark_match_index.py', 'tools/benchmark_deflate.py']
    if args.radix:
        sources.append('stx_zlib/bench/radix_index.hpp')
    hashes = {}
    for name in sources:
        hashes[name] = digest(ROOT / name)
        destination = output / 'source' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    workloads = []
    arguments = ['--files-only'] if args.files_only else []
    if args.greedy_only:
        arguments.append('--greedy-only')
    if args.random_only:
        arguments.append('--random-only')
    if args.random_sweep:
        arguments.append('--random-sweep')
    if args.verify_only:
        if args.png:
            parser.error('--verify-only does not accept PNG timing workloads')
        arguments = ['--verify-only']
    for index, path in enumerate(args.png):
        full = output / f'png-{index}.filtered'
        record = png_filtered(path.resolve(), full)
        filtered = full.read_bytes()
        if args.png_offset >= len(filtered):
            raise RuntimeError('PNG sample offset is outside the filtered bytes')
        prefix = output / f'png-{index}.sample'
        prefix.write_bytes(filtered[args.png_offset:args.png_offset + args.png_bytes])
        record['measured_offset'] = args.png_offset
        record['measured_bytes'] = prefix.stat().st_size
        record['measured_sha256'] = digest(prefix)
        workloads.append(record)
        arguments.append(str(prefix))
    executable = output / 'match-index'
    build = ['clang++', *flags, *link_flags, str(ROOT / sources[0]), '-o', str(executable)]
    command = [str(executable), *arguments]
    record = {'host': platform.platform(), 'machine': platform.machine(), 'compiler': compiler,
              'source_sha256': hashes, 'workloads': workloads, 'commands': [build, command],
              'samples': 9, 'minimum_sample_seconds': 0.05,
              'prefix_sharing': not args.no_sharing,
              'candidate_index': 'critical-bit radix' if args.radix else 'recency lexical tree',
              'radix_finger': args.radix_finger,
              'greedy_only': args.greedy_only,
              'random_only': args.random_only,
              'random_sweep': args.random_sweep,
              'inline_radix': args.inline_radix,
              'small_radix': args.small_radix,
              'singleton_radix': args.singleton_radix,
              'lazy_radix': args.lazy_radix,
              'restrict_index': args.restrict_index,
              'hardware_counters': 'not measured', 'production_encoder': 'unchanged'}
    try:
        with (output / 'compiler.log').open('w') as log:
            subprocess.run(build, stdout=log, stderr=subprocess.STDOUT, check=True)
            if args.assembly:
                assembly = ['clang++', *flags, '-S', str(ROOT / sources[0]), '-o', str(output / 'match-index.s')]
                record['commands'].append(assembly)
                subprocess.run(assembly, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (output / 'results.csv').open('w') as csv, (output / 'workspace.txt').open('w') as log:
            completed = subprocess.run(command, stdout=csv, stderr=log)
        record['exit_code'] = completed.returncode
        completed.check_returncode()
        print((output / 'results.csv').read_text(), end='')
    finally:
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
