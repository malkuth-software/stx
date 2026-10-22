"""Fetch only the pinned test oracle, verifying every source digest."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('destination', type=Path)
args = parser.parse_args()
manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/oracle.json').read_text())
for name, expected in manifest['sha256'].items():
    target = args.destination / name
    if target.exists():
        payload = target.read_bytes()
    else:
        endpoint = 'repos/' + manifest['repository'] + '/contents/' + name + '?ref=' + manifest['revision']
        payload = subprocess.check_output(['gh', 'api', endpoint, '-H', 'Accept: application/vnd.github.raw+json'])
    if hashlib.sha256(payload).hexdigest() != expected:
        raise SystemExit('Oracle digest mismatch: ' + name)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
print('Pinned oracle verified: ' + manifest['revision'])
