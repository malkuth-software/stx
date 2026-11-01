"""Counts instructions, conditional branches, calls and spill annotations of
each probe symbol (and of any helper it tail-calls) in the .s files that
inspect.sh wrote, and writes audit.json beside them.

Counts are a reading aid, not a cost model: stack arguments are not spills,
constant broadcasts are not lane permutations, and instruction count is not
throughput. Read the whole function before concluding anything.
"""
from pathlib import Path
import hashlib
import json
import re
import sys

INSTRUCTION = re.compile(r'^\t[a-z][a-z0-9.]*\s', re.M)
CONDITIONAL = re.compile(r'^\t(?:b\.[a-z]+|cbz|cbnz|tbz|tbnz|j(?!mp)[a-z]+)\s', re.M)
CALL = re.compile(r'^\t(?:callq|bl)\s', re.M)
TAIL = re.compile(r'^\t(?:b|jmp)\s+(_[^\s,]+)', re.M)
PROBE = re.compile(r'^(_stx_[A-Za-z0-9_]+):[^\n]*\n', re.M)


def body_after(text, match):
    end = text.find('.cfi_endproc', match.end())
    if end < 0:
        return None
    return text[match.end():end]


def summary(symbol, body):
    return {
        'symbol': symbol,
        'instructions': len(INSTRUCTION.findall(body)),
        'conditional_branches': len(CONDITIONAL.findall(body)),
        'calls': len(CALL.findall(body)),
        'spill_annotations': body.count('Spill'),
        'reload_annotations': body.count('Reload'),
    }


def audit(path):
    text = path.read_text()
    functions = []
    targets = set()
    for match in PROBE.finditer(text):
        body = body_after(text, match)
        if body is None:
            continue
        record = summary(match[1], body)
        record['tail_targets'] = TAIL.findall(body)
        targets.update(record['tail_targets'])
        functions.append(record)
    helpers = []
    for target in sorted(targets):
        found = re.search(r'^' + re.escape(target) + r':[^\n]*\n', text, re.M)
        if found is None:
            continue
        body = body_after(text, found)
        if body is not None:
            helpers.append(summary(target, body))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {'sha256': digest, 'functions': functions, 'tail_helpers': helpers}


def main():
    root = Path(sys.argv[1])
    records = {}
    for path in sorted(root.glob('*.s')):
        records[path.name] = audit(path)
    report = json.dumps(records, indent=2) + '\n'
    (root / 'audit.json').write_text(report)
    print(report, end='')


if __name__ == '__main__':
    main()
