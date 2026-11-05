"""Retain native pool assembly and compare actual implementation object layouts."""
import json
from pathlib import Path
import re
import shlex
import subprocess


def main():
    layouts = {}
    for profile in ("candidate", "baseline"):
        commands = json.loads(Path(f"build/{profile}/compile_commands.json").read_text())
        entry = next(row for row in commands if row["file"].endswith("/SkDiscardableMemoryPool.cpp"))
        args = shlex.split(entry["command"])
        output = args.index("-o")
        del args[output:output + 2]
        args.remove("-c")
        args += ["-S", "-o", str(Path(f"build/discardable-pool-{profile}.s").resolve()),
                 "-Xclang", "-fdump-record-layouts"]
        result = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True, check=True)
        Path(f"build/discardable-pool-{profile}-layouts.txt").write_text(result.stdout)
        Path(f"build/discardable-pool-{profile}-compiler.log").write_text(result.stderr)
        layouts[profile] = {}
        for block in result.stdout.split("*** Dumping AST Record Layout"):
            for name in ("DiscardableMemoryPool", "PoolDiscardableMemory"):
                if re.match(r"\s*0 \| class \(anonymous namespace\)::" + name + r"\s*\n", block):
                    match = re.search(r"\[sizeof=(\d+).*?align=(\d+)", block, re.S)
                    if match is None:
                        raise RuntimeError("Missing layout extent for " + name)
                    layouts[profile][name] = {"bytes": int(match[1]), "alignment": int(match[2])}
        assert len(layouts[profile]) == 2, layouts[profile]
    Path("build/discardable-pool-layouts.json").write_text(json.dumps(layouts, indent=2) + "\n")
    print(json.dumps(layouts))


if __name__ == "__main__":
    main()
