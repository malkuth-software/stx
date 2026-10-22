"""Run the deterministic suite of an instrumented build and report the result.

    python3 stx_conv/tools/sanitizer_check.py <sanitizer-build> <new-record-directory>

Records the source digests and the complete test output in the record
directory, prints that output, and ends with PASS or STOP. Every sanitizer
report stops its test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import os
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('record', type=Path)
args = parser.parse_args()
args.record.mkdir(parents=True, exist_ok=False)
manifest = {}
root = Path(__file__).resolve().parents[1]
for path in sorted(root.rglob('*')):
    if path.is_file() and path.suffix in {'.cpp', '.hpp', '.inc', '.json', '.py', '.txt'}:
        manifest[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
(args.record / 'source_hashes.json').write_text(json.dumps(manifest, indent=2) + '\n')
environment = os.environ.copy()
# The MinGW runtime on Windows has no leak sanitizer.
detect_leaks = '0' if sys.platform == 'win32' else '1'
environment['ASAN_OPTIONS'] = 'detect_leaks=' + detect_leaks + ':halt_on_error=1'
environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
completed = subprocess.run(['ctest', '--test-dir', str(args.build), '--output-on-failure', '--no-tests=error'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)
(args.record / 'diagnostics.txt').write_bytes(completed.stdout)
sys.stdout.write(completed.stdout.decode('utf-8', errors='replace'))
passed = completed.returncode == 0
print('PASS' if passed else 'STOP')
raise SystemExit(0 if passed else 1)
