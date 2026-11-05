"""Symbol-level users of library objects, for prune decisions.

For each named source, lists the other library objects and test programs that reference any
external symbol the source's object defines. Only objects of sources in the build's
compile_commands.json count: a build directory keeps objects of sources it no longer compiles,
and those stale objects must not be mistaken for users.

Usage (on the build host, with llvm-nm on PATH):
    python3 tools/link_closure.py [--build build/candidate] SkMatrix22.cpp SkPatchUtils.cpp ...

A source with no users is a prune candidate, not proof: check headers that include it, other
platforms' builds (Windows-only sources do not appear in a macOS build), and the consumer.
"""
import json
import pathlib
import subprocess
import sys


def symbols(path, defined):
    flags = ["--defined-only", "--no-weak"] if defined else ["-u"]
    output = subprocess.run(["llvm-nm", *flags, str(path)], capture_output=True, text=True).stdout
    names = set()
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        if defined:
            if len(parts) >= 3 and parts[1] in "TDBSRVW":
                names.add(parts[2])
        else:
            names.add(parts[-1])
    return names


def main():
    arguments = sys.argv[1:]
    build = pathlib.Path("build/candidate")
    if arguments[:1] == ["--build"]:
        build = pathlib.Path(arguments[1])
        arguments = arguments[2:]
    commands = json.loads((build / "compile_commands.json").read_text())
    compiled = {row["file"].split("/skia/", 1)[-1] for row in commands}
    library = {}
    for obj in build.glob("CMakeFiles/stx_skia.dir/**/*.o"):
        relative = str(obj).split("stx_skia.dir/")[-1][:-2]
        if relative.split("/skia/", 1)[-1] in compiled or relative.replace("skia/", "", 1) in compiled:
            library[relative] = obj
    undefined = {name: symbols(obj, False) for name, obj in library.items()}
    programs = {str(p).split("CMakeFiles/")[-1]: symbols(p, False)
                for p in build.glob("CMakeFiles/stx_skia_*.dir/**/*.o")}
    for target in arguments:
        matches = [name for name in library if name.endswith(target)]
        if not matches:
            print(f"== {target}: not compiled")
            continue
        name = matches[0]
        defined = symbols(library[name], True)
        users = sorted(other for other, names in undefined.items() if other != name and names & defined)
        tests = sorted(p for p, names in programs.items() if names & defined)
        print(f"== {name}: {len(defined)} symbols; library users: {users or 'none'}; "
              f"tests: {tests or 'none'}")


if __name__ == "__main__":
    main()
