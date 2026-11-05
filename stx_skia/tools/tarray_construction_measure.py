import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path


def run(profile):
    output = subprocess.check_output(
        [f"build/{profile}/stx_skia_tarray_construction_tests", "--benchmark"], text=True)
    rows = {}
    for line in output.splitlines():
        if line and line[0].isdigit():
            count, mode, iterations, seconds, checksum = line.split(',')
            rows[(count, mode)] = (int(iterations), float(seconds), int(checksum))
    if len(rows) != 6:
        raise RuntimeError('Missing timing rows')
    return rows


samples = {}
run('candidate')
run('baseline')
for trial in range(9):
    order = ['baseline', 'candidate'] if trial % 2 == 0 else ['candidate', 'baseline']
    for profile in order:
        for key, (iterations, seconds, checksum) in run(profile).items():
            entry = samples.setdefault(key, {'iterations': iterations, 'checksum': checksum,
                                              'candidate': [], 'baseline': []})
            if entry['iterations'] != iterations or entry['checksum'] != checksum:
                raise RuntimeError('Timing workload disagreement')
            entry[profile].append(seconds)
rows = []
for (count, mode), entry in sorted(samples.items()):
    ratio = statistics.median(entry['candidate']) / statistics.median(entry['baseline'])
    entry.update(count=int(count), mode='copy assignment' if mode == '0' else 'disjoint span reset',
                 candidate_over_reference=ratio)
    rows.append(entry)
    print(count, entry['mode'], 'candidate/reference', round(ratio, 4))
report = {'machine': platform.machine(), 'platform': platform.platform(),
          'compiler': subprocess.check_output(['clang++', '--version'], text=True).splitlines()[0],
          'source_sha256': hashlib.sha256(Path('skia/include/private/SkTArray.h').read_bytes()).hexdigest(),
          'fixture_sha256': hashlib.sha256(Path('tests/tarray_construction.cpp').read_bytes()).hexdigest(),
          'rows': rows,
          'limits': 'M4 native release. Reused capacity, uint32 copies. Includes checked operation boundaries and checksum, excludes initial allocation; no whole-renderer speed claim.'}
Path('build/tarray-construction-measurements.json').write_text(json.dumps(report, indent=2)+'\n')
