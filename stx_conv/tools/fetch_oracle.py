"""Fetch only the pinned test oracle, verifying every source digest.

    python3 stx_conv/tools/fetch_oracle.py <destination>

Files already in <destination> are verified, not fetched again. Missing
files are taken from a shallow git fetch of the pinned Plan Paint revision
(a public repository; no credentials), checked out without line-ending
conversion. Any digest mismatch stops with an error and nothing is used.
The oracle is test-only: it is compiled into the differential tests and
never into the library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('destination', type=Path)
args = parser.parse_args()
manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/oracle.json').read_text())
missing = [name for name in manifest['sha256'] if not (args.destination / name).exists()]
checkout = None
if missing:
    checkout = Path(tempfile.mkdtemp(prefix='stx-conv-oracle-'))
    url = 'https://github.com/' + manifest['repository'] + '.git'
    subprocess.run(['git', 'init', '-q', str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'fetch', '-q', '--depth', '1', url, manifest['revision']], check=True)
    subprocess.run(['git', '-c', 'core.autocrlf=false', '-C', str(checkout), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)
for name, expected in manifest['sha256'].items():
    target = args.destination / name
    if target.exists():
        payload = target.read_bytes()
    else:
        payload = (checkout / name).read_bytes()
    if hashlib.sha256(payload).hexdigest() != expected:
        raise SystemExit('Oracle digest mismatch: ' + name)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
print('Pinned oracle verified: ' + manifest['revision'])
