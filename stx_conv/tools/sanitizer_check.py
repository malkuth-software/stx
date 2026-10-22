"""Run the deterministic sanitizer suite; expose only PASS or STOP."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('record', type=Path)
args = parser.parse_args()
args.record.mkdir(parents=True, exist_ok=False)
manifest = {}
root = Path(__file__).resolve().parents[1]
for path in sorted(root.rglob('*')):
    if path.is_file() and path.suffix in {'.cpp', '.hpp', '.inc', '.json', '.py', '.txt'}:
        manifest[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
(args.record / 'source_hashes.json').write_text(json.dumps(manifest, indent=2) + '\n')
environment = os.environ.copy()
environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
completed = subprocess.run(['ctest', '--test-dir', str(args.build), '--output-on-failure', '--no-tests=error'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)
(args.record / 'diagnostics.txt').write_bytes(completed.stdout)
passed = completed.returncode == 0
print('PASS' if passed else 'STOP')
raise SystemExit(0 if passed else 1)
