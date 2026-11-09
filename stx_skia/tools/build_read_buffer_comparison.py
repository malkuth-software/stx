"""Replace only the reader header/implementation for an attribution-controlled native comparison."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    root = Path.cwd()
    destination = root / "build/read-buffer-prior"
    override = destination / "include/src/core"
    override.mkdir(parents=True, exist_ok=True)
    expected = {'SkReadBuffer.h': '63841771855a61f11d691837ccce6e8b9a0fda5fce5380ab620fd8ca4b203b1c', 'SkReadBuffer.cpp': '0a0a7d70524c0482318cf42f7707c777a8fb29f9293189ee936bf513a45869c3'}
    for name in ("SkReadBuffer.h", "SkReadBuffer.cpp"):
        original = root / "build/read-buffer-prior-source/src/core" / name
        content = original.read_bytes()
        assert hashlib.sha256(content).hexdigest() == expected[name], "Expected 6c91ea4 reader source"
        (override / name).write_bytes(content)
    commands = json.loads((root / "build/candidate/compile_commands.json").read_text())
    recorded = []
    for source, name in (("/tests/read_buffer.cpp", "test.o"),
                         ("/skia/src/core/SkReadBuffer.cpp", "reader.o")):
        entry = next(row for row in commands if row["file"].endswith(source))
        args = shlex.split(entry["command"])
        output_index = args.index("-o") + 1
        args[output_index] = str(destination / name)
        if name == "reader.o":
            args[-1] = str(override / "SkReadBuffer.cpp")
        args[1:1] = ["-I" + str(destination / "include"), "-DSTX_SKIA_REFERENCE=1"]
        subprocess.run(args, cwd=entry["directory"], check=True)
        recorded.append(args)
    lines = subprocess.check_output(
        ["ninja", "-C", "build/candidate", "-t", "commands", "stx_skia_read_buffer_tests"],
        text=True).splitlines()
    args = shlex.split(lines[-1])
    assert args[:2] == [":", "&&"] and args[-2:] == ["&&", ":"]
    args = args[2:-2]
    test_object = "CMakeFiles/stx_skia_read_buffer_tests.dir/tests/read_buffer.cpp.o"
    position = args.index(test_object)
    args[position:position + 1] = [str(destination / "test.o"), str(destination / "reader.o")]
    args[args.index("-o") + 1] = str(destination / "stx_skia_read_buffer_tests")
    subprocess.run(args, cwd=root / "build/candidate", check=True)
    recorded.append(args)
    (destination / "commands.json").write_text(json.dumps(recorded, indent=2) + "\n")
    prior = subprocess.check_output([str(destination / "stx_skia_read_buffer_tests")], text=True)
    current = subprocess.check_output(["build/candidate/stx_skia_read_buffer_tests"], text=True)
    assert prior == current
    print(prior.strip())
    print("Controlled comparison: only reader header/implementation differs; current library dependencies retained")


if __name__ == "__main__":
    main()
