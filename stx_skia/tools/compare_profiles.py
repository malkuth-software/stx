#!/usr/bin/env python3
"""Compares the corpora two stx_skia profiles wrote.

Usage: compare_profiles.py <reference tree> <candidate tree>

Every file named in tests/corpora.txt must exist in both trees. A file listed in
tests/intentional_differences.txt must differ; every other file must match byte for
byte. Prints one line per file and exits 1 on any failure, 2 on a usage error.
"""

import filecmp
import pathlib
import sys

STX_SKIA_ROOT = pathlib.Path(__file__).resolve().parent.parent


def read_listed_names(path: pathlib.Path, skip_first_field: bool) -> list[str]:
    """Returns the names a list file declares: whitespace-separated fields on lines that are
    neither blank nor comments. With skip_first_field, the first field (a program) is not
    a name; without it, only the first field is."""
    names: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split()
        if skip_first_field:
            names.extend(fields[1:])
        else:
            names.append(fields[0])
    return names


def compare(reference: pathlib.Path, candidate: pathlib.Path, root: pathlib.Path) -> int:
    corpus_names = read_listed_names(root / "tests" / "corpora.txt", skip_first_field=True)
    intentional = set(read_listed_names(root / "tests" / "intentional_differences.txt",
                                        skip_first_field=False))
    failures = 0
    unknown = sorted(intentional.difference(corpus_names))
    for name in unknown:
        print(f"FAIL {name}: listed as an intentional difference but no corpus program writes it")
        failures += 1
    for name in corpus_names:
        reference_file = reference / name
        candidate_file = candidate / name
        if not reference_file.is_file() or not candidate_file.is_file():
            print(f"FAIL {name}: missing from a profile")
            failures += 1
            continue
        identical = filecmp.cmp(reference_file, candidate_file, shallow=False)
        if name in intentional:
            if identical:
                print(f"FAIL {name}: listed as an intentional difference but identical")
                failures += 1
            else:
                print(f"differs (intentional) {name}")
        elif identical:
            print(f"identical {name}")
        else:
            print(f"FAIL {name}: differs from the reference profile")
            failures += 1
    print(f"{len(corpus_names)} corpus files, {len(intentional)} intentional differences, "
          f"{failures} failures")
    if failures != 0:
        return 1
    return 0


def main(arguments: list[str]) -> int:
    if len(arguments) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    reference = pathlib.Path(arguments[0])
    candidate = pathlib.Path(arguments[1])
    return compare(reference, candidate, STX_SKIA_ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
