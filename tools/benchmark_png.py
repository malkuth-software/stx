#!/usr/bin/env python3
"""Measure stored-block PNG workloads; keep oracle and candidate build flags explicit."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
from validate_png import ROOT, verify


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--libpng', type=Path, required=True)
    parser.add_argument('--zlib', type=Path, required=True)
    parser.add_argument('--workloads', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, default=ROOT / 'tools/codec_bench/png_benchmark_baseline_decoder.cpp.txt')
    args = parser.parse_args()
    png = args.libpng.resolve()
    zlib = args.zlib.resolve()
    workloads = args.workloads.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline = args.baseline.resolve()
    shutil.copyfile(baseline, output / 'baseline.cpp')
    png_pin = verify(png, ROOT / 'stx_png/tests/libpng_oracle_sha256.json')
    zlib_pin = verify(zlib, ROOT / 'stx_zlib/tests/oracle_sha256.json')
    version = subprocess.check_output(['clang++', '--version'], text=True)
    if 'clang version 22.1.8' not in version:
        raise RuntimeError('LLVM 22.1.8 required')
    record = {'libpng_revision': png_pin['revision'], 'zlib_revision': zlib_pin['revision'],
              'machine': platform.platform(), 'compiler': version, 'commands': [],
              'baseline_sha256': hashlib.sha256(baseline.read_bytes()).hexdigest(),
              'timing': '50ms calibration, rotating order, 2 warmup + 7 paired rounds',
              'workloads': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in workloads.glob('*.png')},
              'sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in (
                  'stx_png/src/decoder.cpp','stx_png/include/stx_png/decoder.hpp',
                  'stx_png/src/encoder.cpp','stx_zlib/src/inflate.cpp',
                  'tools/codec_bench/seam/png_decoder.hpp',
                  'tools/codec_bench/seam/gui_forms/compression/inflate.hpp',
                  'tools/codec_bench/png_benchmark.cpp','stx_png/tests/png_oracle.c',
                  'tools/benchmark_png.py')}}
    includes = ['-I' + str(output), '-I' + str(png), '-I' + str(zlib), '-I' + str(ROOT / 'tools/codec_bench/seam'), '-I' + str(ROOT / 'stx_zlib/include'), '-I' + str(ROOT / 'stx_png/include')]
    shutil.copyfile(png / 'scripts/pnglibconf.h.prebuilt', output / 'pnglibconf.h')
    native_neon = platform.machine().lower() in ('arm64', 'aarch64')
    record['oracle_simd'] = 'NEON intrinsics' if native_neon else 'portable C'
    png_options = ['-DPNG_ARM_NEON_OPT=2'] if native_neon else ['-DPNG_ARM_NEON_OPT=0']
    try:
        objects = []
        for root, names in ((zlib, ['adler32','crc32','inflate','inffast','inftrees','zutil','deflate','trees']),
                            (png, ['png','pngerror','pngget','pngmem','pngpread','pngread','pngrio',
                                   'pngrtran','pngrutil','pngset','pngtrans','pngwio','pngwrite','pngwtran','pngwutil'])):
            for name in names:
                obj = output / (name + '.o')
                command = ['clang', '-O3', *includes, *png_options, '-c', str(root / (name + '.c')), '-o', str(obj)]
                record['commands'].append(command)
                subprocess.run(command, check=True)
                objects.append(str(obj))
        if native_neon:
            for name in ('arm_init', 'filter_neon_intrinsics', 'palette_neon_intrinsics'):
                obj = output / (name + '.o')
                command = ['clang', '-O3', *includes, *png_options, '-c',
                           str(png / 'arm' / (name + '.c')), '-o', str(obj)]
                record['commands'].append(command)
                subprocess.run(command, check=True)
                objects.append(str(obj))
        oracle = output / 'oracle.o'
        command = ['clang', '-O3', *includes, '-c', str(ROOT / 'stx_png/tests/png_oracle.c'), '-o', str(oracle)]
        record['commands'].append(command)
        subprocess.run(command, check=True)
        for mode, flags in (('vector', []), ('no-vector', ['-fno-vectorize', '-fno-slp-vectorize', '-DGUI_FORMS_DISABLE_VECTOR_HINTS=1'])):
            baseline_object = output / ('baseline-' + mode + '.o')
            command = ['clang++', '-std=c++20', '-O3', *includes, *flags,
                       '-Ddecode_png_samples=baseline_decode_png_samples',
                       '-Dinspect_png_samples=baseline_inspect_png_samples',
                       '-c', str(output / 'baseline.cpp'), '-o', str(baseline_object)]
            record['commands'].append(command)
            subprocess.run(command, check=True)
            binary = output / ('benchmark-' + mode)
            command = ['clang++', '-std=c++20', '-O3', *includes, *flags,
                       str(ROOT / 'stx_png/src/decoder.cpp'), str(ROOT / 'stx_png/src/encoder.cpp'),
                       str(ROOT / 'stx_zlib/src/inflate.cpp'), str(ROOT / 'stx_zlib/src/deflate.cpp'),
                       str(ROOT / 'stx_zlib/src/checksum.cpp'),
                       str(ROOT / 'tools/codec_bench/png_benchmark.cpp'),
                       str(baseline_object), str(oracle), *objects, '-o', str(binary)]
            record['commands'].append(command)
            subprocess.run(command, check=True)
            command = [str(binary), str(workloads)]
            record['commands'].append(command)
            with (output / (mode + '.csv')).open('w') as log:
                subprocess.run(command, stdout=log, check=True)
        targets = [('arm64', [])]
        if platform.system() == 'Darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            targets.append(('x64-inspection', ['-target', 'x86_64-apple-macos14', '-isysroot', sdk,
                                             '-march=x86-64', '-mtune=generic', '-msse2', '-mno-avx']))
        for name, flags in targets:
            command = ['clang++', '-std=c++20', '-O3', '-S', '-Rpass=loop-vectorize',
                       '-Rpass-missed=loop-vectorize', *includes, *flags,
                       str(ROOT / 'stx_png/src/decoder.cpp'), '-o', str(output / (name + '.s'))]
            record['commands'].append(command)
            with (output / (name + '-remarks.txt')).open('w') as log:
                subprocess.run(command, stderr=log, check=True)
    finally:
        (output / 'run.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
