"""Retain native conversion assembly and vectorization remarks for both profiles."""
import json
from pathlib import Path
import shlex
import subprocess


def main():
    output = Path("build/pixel-conversion-assembly")
    output.mkdir(exist_ok=True)
    for profile in ["baseline", "candidate"]:
        commands = json.loads(Path(f"build/{profile}/compile_commands.json").read_text())
        matches = [entry for entry in commands if entry["file"].endswith("/SkConvertPixels.cpp")]
        if len(matches) != 1:
            raise RuntimeError("Expected exactly one conversion compile command")
        command = shlex.split(matches[0]["command"])
        assembly = []
        index = 0
        while index < len(command):
            flag = command[index]
            if flag in ["-o", "-MF", "-MT", "-MQ"]:
                index += 2
                continue
            if flag not in ["-c", "-MD", "-MMD"]:
                assembly.append(flag)
            index += 1
        assembly.extend(["-S", "-o", str((output / f"{profile}.s").resolve()),
                         "-Rpass=loop-vectorize", "-Rpass-missed=loop-vectorize"])
        with (output / f"{profile}-remarks.txt").open("w") as log:
            subprocess.run(assembly, cwd=matches[0]["directory"], check=True, stderr=log)
        print(f"{profile}: assembly and remarks retained")


if __name__ == "__main__":
    main()
