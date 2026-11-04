#!/usr/bin/env python3
"""Compare the owned encoder with source-pinned zlib, preserving build evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def png_filtered(path, destination):
    """Extract bounded IDAT output, preserving PNG filters for encoder timing."""
    maximum = 16 * 1024 * 1024
    with path.open('rb') as source:
        png = source.read(maximum + 1)
    if len(png) > maximum or png[:8] != b'\x89PNG\r\n\x1a\n':
        raise RuntimeError('PNG signature/size: ' + str(path))
    position = 8
    idat = bytearray()
    ihdr = None
    complete = False
    while position + 12 <= len(png):
        length = int.from_bytes(png[position:position + 4], 'big')
        end = position + 8 + length
        if end + 4 > len(png):
            raise RuntimeError('PNG chunk extent')
        kind = png[position + 4:position + 8]
        payload = png[position + 8:end]
        if zlib.crc32(kind + payload) != int.from_bytes(png[end:end + 4], 'big'):
            raise RuntimeError('PNG CRC')
        if kind == b'IHDR':
            ihdr = payload.hex()
        elif kind == b'IDAT':
            idat.extend(payload)
        elif kind == b'IEND':
            complete = length == 0 and end + 4 == len(png)
            break
        position = end + 4
    if not complete or ihdr is None or not idat:
        raise RuntimeError('PNG missing required chunks')
    decoder = zlib.decompressobj()
    filtered = decoder.decompress(idat, maximum + 1)
    if len(filtered) > maximum or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise RuntimeError('PNG IDAT stream/size')
    destination.write_bytes(filtered)
    return {'path': str(path), 'png_sha256': digest(path), 'ihdr_hex': ihdr,
            'filtered_bytes': len(filtered), 'filtered_sha256': digest(destination),
            'extractor_zlib': zlib.ZLIB_RUNTIME_VERSION}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--no-vector', action='store_true')
    parser.add_argument('--profile', action='store_true', help='time block-level stages in an instrumented source copy')
    parser.add_argument('--png', type=Path, action='append', default=[], help='also benchmark filtered IDAT bytes from a local PNG')
    parser.add_argument('--inspect-x64', action='store_true', help='emit generic SSE2 assembly without executing it')
    parser.add_argument('--workload', type=int, choices=range(3))
    parser.add_argument('--level', type=int, choices=range(10), default=6)
    args = parser.parse_args()
    oracle = args.oracle.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'stx_zlib/tests/oracle_sha256.json').read_text())
    for name, expected in manifest['files'].items():
        if digest(oracle / name) != expected:
            raise RuntimeError('oracle digest mismatch: ' + name)
    compiler = subprocess.check_output(['clang++', '--version'], text=True)
    if 'clang version 22.1.8' not in compiler:
        raise RuntimeError('LLVM 22.1.8 required')
    flags = ['-O3', '-DNDEBUG']
    cxx_flags = ['-std=c++20']
    link_flags = []
    if platform.system() == 'Darwin':
        runtime = Path(os.environ['GUI_FORMS_LLVM_RUNTIME'])
        cxx_flags += ['-stdlib=libc++', '-nostdinc++', '-isystem', str(runtime / 'include/c++/v1')]
        link_flags += ['-L' + str(runtime / 'lib'), '-Wl,-rpath,' + str(runtime / 'lib'), '-lunwind']
    commands = []
    sources = ['stx_zlib/src/deflate.cpp', 'stx_zlib/include/stx_zlib/deflate.hpp',
               'stx_zlib/include/stx_zlib/inflate.hpp', 'stx_zlib/bench/deflate.cpp',
               'tools/benchmark_deflate.py']
    hashes = {}
    for name in sources:
        hashes[name] = digest(ROOT / name)
        destination = output / 'source' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    record = {'host': platform.platform(), 'machine': platform.machine(), 'compiler': compiler,
              'oracle_revision': manifest['revision'], 'source_sha256': hashes,
              'flags': flags, 'cxx_flags': cxx_flags, 'link_flags': link_flags,
              'no_vector': args.no_vector, 'profile': args.profile, 'lto': False, 'commands': commands,
              'sampling': 'calibrate >=10ms, two warmup pairs, nine timed pairs with alternating order; reset included; allocation excluded'}
    if platform.system() == 'Darwin':
        record['hardware'] = subprocess.check_output(
            ['/usr/sbin/sysctl', 'machdep.cpu.brand_string', 'hw.memsize'], text=True)
    try:
        objects = []
        for source in ('adler32', 'crc32', 'deflate', 'inflate', 'inffast', 'inftrees', 'trees', 'zutil'):
            target = str(output / (source + '.o'))
            command = ['clang', *flags, '-I' + str(oracle), '-c', str(oracle / (source + '.c')), '-o', target]
            commands.append(command)
            subprocess.run(command, check=True)
            objects.append(target)
        extra = ['-fno-vectorize', '-fno-slp-vectorize'] if args.no_vector else []
        source = ROOT / 'stx_zlib/src/deflate.cpp'
        if args.profile:
            instrumented = source.read_text()
            stages = ['tokenize', 'prepare_dynamic', 'emit_tokens', 'encode_block']
            for index, stage in enumerate(stages):
                start = instrumented.index('DeflateStream::' + stage + '(')
                brace = instrumented.index('{', start)
                instrumented = (instrumented[:brace + 1] +
                                f'\n    stx_profile::Scope profile_scope{{{index}}};' + instrumented[brace + 1:])
            prefix = '''#include <chrono>
#include <cstdio>
#include <cstdint>
#include <array>
namespace stx_profile {
struct Totals final {
    std::array<std::uint64_t, 5> ns{};
    ~Totals() {
        constexpr std::array<const char*, 5> names{"adler", "tokenize", "dynamic_tree", "emit_tokens", "block_inclusive"};
        for (unsigned index = 0; index < 5; ++index)
            std::fprintf(stderr, "%s,%llu\\n", names[index], static_cast<unsigned long long>(ns[index]));
    }
};
Totals totals{};
struct Scope final {
    unsigned stage = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Scope() {
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        totals.ns[stage] += static_cast<std::uint64_t>(elapsed.count());
    }
};
}
'''
            source = output / 'profile-deflate.cpp'
            source.write_text(prefix + instrumented)
        common = ['clang++', *cxx_flags, *flags, *extra, '-Wall', '-Wextra', '-Wpedantic',
                  '-I' + str(ROOT / 'stx_zlib/include'), '-I' + str(oracle)]
        executable = str(output / 'benchmark')
        command = [*common, '-Rpass=loop-vectorize', '-Rpass-missed=loop-vectorize',
                   str(source), str(ROOT / 'stx_zlib/src/checksum.cpp'), str(ROOT / 'stx_zlib/bench/deflate.cpp'),
                   *objects, *link_flags, '-o', executable]
        commands.append(command)
        with (output / 'compiler.log').open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        command = [*common, '-S', str(ROOT / 'stx_zlib/src/deflate.cpp'), '-o', str(output / 'candidate.s')]
        commands.append(command)
        subprocess.run(command, check=True)
        command = [*common, '-fstack-usage', '-c', str(ROOT / 'stx_zlib/src/deflate.cpp'),
                   '-o', str(output / 'candidate-stack.o')]
        commands.append(command)
        subprocess.run(command, check=True)
        command = [executable]
        if args.workload is not None:
            command += [str(args.workload), str(args.level)]
        commands.append(command)
        with (output / 'benchmark.csv').open('w') as log, (output / 'stages.csv').open('w') as stages_log:
            subprocess.run(command, cwd=output, stdout=log, stderr=stages_log, check=True)
        print((output / 'benchmark.csv').read_text())
        if args.profile:
            print((output / 'stages.csv').read_text())
        record['png_fixtures'] = []
        for index, path in enumerate(args.png):
            fixture = output / f'png-{index}.filtered'
            details = png_filtered(path.resolve(), fixture)
            details['results'] = f'png-{index}.csv'
            record['png_fixtures'].append(details)
            command = [executable, str(fixture)]
            commands.append(command)
            with (output / details['results']).open('w') as log:
                subprocess.run(command, cwd=output, stdout=log, check=True)
            print(str(path) + '\n' + (output / details['results']).read_text())
        if args.inspect_x64:
            sdk = subprocess.check_output(['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
            command = ['clang++', *cxx_flags, *flags, *extra, '--target=x86_64-apple-macos14',
                       '-isysroot', sdk, '-march=x86-64', '-mtune=generic', '-msse2', '-mno-avx',
                       '-Rpass=loop-vectorize', '-Rpass-missed=loop-vectorize',
                       '-I' + str(ROOT / 'stx_zlib/include'), '-S', str(ROOT / 'stx_zlib/src/deflate.cpp'),
                       '-o', str(output / 'candidate-sse2.s')]
            commands.append(command)
            with (output / 'candidate-sse2-compiler.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    finally:
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
