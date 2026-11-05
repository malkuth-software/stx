"""Emit paired native assembly for the bounded hash workloads."""
import json
from pathlib import Path
import shlex
import subprocess

for profile in ("candidate", "baseline"):
    commands = json.loads(Path(f"build/{profile}/compile_commands.json").read_text())
    entry = next(row for row in commands if row["file"].endswith("/tests/hash_storage.cpp"))
    args = shlex.split(entry["command"])
    output = args.index("-o")
    del args[output:output + 2]
    args.remove("-c")
    args += ["-S", "-o", str(Path(f"build/hash-storage-{profile}.s").resolve()),
             "-Rpass-missed=inline"]
    with Path(f"build/hash-storage-{profile}-inline.log").open("w") as log:
        subprocess.run(args, cwd=entry["directory"], stderr=log, check=True)
    print(profile, "assembly and missed-inline remarks retained")
