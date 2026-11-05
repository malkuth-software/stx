#!/usr/bin/env python3
"""House-style spelling check for stx_skia/, against a shrink-only allowance.

The files imported from Skia into stx_skia/skia/ still carry house-style
findings while they are converted. stx_skia/house_style_allowance.txt lists,
for each such file, how many findings check_house_style reports in it. This
check runs check_house_style over every C++ file in stx_skia/ and fails when

- a file not listed has a finding (stx_skia's own files are never listed);
- a listed file has more findings than listed;
- a listed file has fewer findings than listed, or none, or no longer exists:
  lower the count or remove the entry, so the allowance only shrinks.

Run from anywhere: python3 tools/check_stx_skia_style.py [--root <stx_skia>]
[--allowance <file>]. Prints each failure and a summary; exits 1 on failure.
--write-allowance rewrites the allowance from the current findings (for the
conversion that empties an entry, never to admit a new one: review the diff).
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
from pathlib import Path
import sys

import harness_link  # noqa: F401  (puts the harness's checkers on sys.path)
import check_house_style

SOURCE: Path = Path(__file__).resolve().parents[1]
DEFAULT_ROOT: Path = SOURCE / 'stx_skia'
ALLOWANCE_NAME: str = 'house_style_allowance.txt'
ALLOWANCE_HEADER: str = (
    '# House-style findings still allowed in files imported from Skia, as\n'
    '# "<count> <path under stx_skia/>". tools/check_stx_skia_style.py fails on a\n'
    '# finding in an unlisted file and on any count that differs from the file\'s\n'
    '# findings, so entries only fall until the list is empty. Never add an entry.\n')


def read_allowance(path: Path) -> dict[str, int]:
    """The allowance as relative path (forward slashes) to finding count."""
    allowance: dict[str, int] = {}
    line: str = ''
    for line in path.read_text(encoding='utf-8').splitlines():
        stripped: str = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        fields: list[str] = stripped.split()
        if len(fields) != 2 or not fields[0].isdigit():
            raise ValueError('malformed allowance line: ' + line)
        if fields[1] in allowance:
            raise ValueError('duplicate allowance entry: ' + fields[1])
        allowance[fields[1]] = int(fields[0])
    return allowance


def count_findings(root: Path) -> dict[str, int]:
    """Finding counts for every C++ file under root that has any, by relative path."""
    counts: dict[str, int] = {}
    paths: list[Path] = check_house_style.collect([str(root)])
    path: Path
    for path in paths:
        source: str = path.read_text(encoding='utf-8-sig')
        findings: list[check_house_style.Finding] = check_house_style.inspect(source)
        if findings:
            relative: str = path.relative_to(root).as_posix()
            counts[relative] = len(findings)
    return counts


@dataclass(frozen=True)
class Disagreement:
    """One difference between the allowance and the counted findings. growth is true for a
    finding the allowance does not cover (an unlisted file or a raised count) and false for
    an entry that is now too high (a lowered count, a clean or a removed file)."""
    relative: str
    growth: bool
    message: str


def compare(allowance: dict[str, int], counts: dict[str, int]) -> list[Disagreement]:
    """Every disagreement between the allowance and the counted findings, by path."""
    disagreements: list[Disagreement] = []
    relative: str = ''
    for relative in sorted(counts):
        found: int = counts[relative]
        if relative not in allowance:
            disagreements.append(Disagreement(relative, True, str(found) + ' findings in a file the allowance does not list'))
            continue
        allowed: int = allowance[relative]
        if found > allowed:
            disagreements.append(Disagreement(relative, True, str(found) + ' findings, more than the ' + str(allowed) + ' allowed'))
        elif found < allowed:
            disagreements.append(Disagreement(relative, False, str(found) + ' findings, fewer than the ' + str(allowed)
                                              + ' listed; lower the entry'))
    for relative in sorted(allowance):
        if relative not in counts:
            disagreements.append(Disagreement(relative, False, 'listed but has no findings (or no longer exists); remove the entry'))
    return disagreements


def format_allowance(counts: dict[str, int]) -> str:
    lines: list[str] = [ALLOWANCE_HEADER]
    relative: str = ''
    for relative in sorted(counts):
        lines.append(str(counts[relative]) + ' ' + relative + '\n')
    text: str = ''.join(lines)
    return text


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=DEFAULT_ROOT)
    parser.add_argument('--allowance', type=Path, default=None)
    parser.add_argument('--write-allowance', action='store_true')
    options: argparse.Namespace = parser.parse_args(arguments)
    root: Path = options.root.resolve()
    allowance_path: Path = options.allowance if options.allowance is not None else root / ALLOWANCE_NAME
    counts: dict[str, int] = count_findings(root)
    allowance: dict[str, int] = read_allowance(allowance_path)
    disagreements: list[Disagreement] = compare(allowance, counts)
    growth_count: int = 0
    disagreement: Disagreement
    for disagreement in disagreements:
        print(disagreement.relative + ': ' + disagreement.message)
        if disagreement.growth:
            growth_count += 1
    if options.write_allowance:
        # Only shrinking is written: a new file or a raised count stays a failure.
        if growth_count != 0:
            print('allowance not written: it may only shrink.')
            return 1
        allowance_path.write_text(format_allowance(counts), encoding='utf-8', newline='\n')
        print('wrote ' + str(len(counts)) + ' entries to ' + str(allowance_path))
        return 0
    total: int = sum(counts.values())
    print(str(len(counts)) + ' files with ' + str(total) + ' allowed findings; ' + str(len(disagreements)) + ' failures.')
    if disagreements:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
