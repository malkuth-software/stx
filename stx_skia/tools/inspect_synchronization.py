"""Emit small native probes for the retained synchronization fast paths."""
import json
from pathlib import Path
import shlex
import subprocess

SOURCE = '''#include "include/private/SkMutex.h"
#include "include/private/SkOnce.h"
#include "include/private/SkSemaphore.h"
static void mark(int& value) { value = 17; }
extern "C" void semaphore_cycle(SkSemaphore& semaphore) {
    semaphore.wait();
    semaphore.signal();
}
extern "C" void mutex_cycle(SkMutex& mutex) {
    SkAutoMutexExclusive guard(mutex);
}
extern "C" void once_call(SkOnce& once, int& value) { once(mark, value); }
'''


def main():
    source = Path("build/synchronization-probe.cpp").resolve()
    source.write_text(SOURCE)
    for profile in ("baseline", "candidate"):
        commands = json.loads(Path(f"build/{profile}/compile_commands.json").read_text())
        entry = next(row for row in commands if row["file"].endswith("/tests/synchronization.cpp"))
        args = shlex.split(entry["command"])
        output = args.index("-o")
        del args[output:output + 2]
        args.remove("-c")
        args[args.index(entry["file"])] = str(source)
        args += ["-S", "-o", str(Path(f"build/synchronization-{profile}.s").resolve())]
        subprocess.run(args, cwd=entry["directory"], check=True)
        print(profile, "native synchronization probes retained")


if __name__ == "__main__":
    main()
