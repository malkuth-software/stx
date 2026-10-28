"""Inclusive sample shares per function from a macOS `sample` call graph.

Counts only samples inside a scope frame (for example GUI.Forms' Skia renderer), so other work
in the process does not dilute the shares. A frame matching a pattern is counted once per
stack: nested matches under an earlier match of the same pattern are not added again.

Usage:
    sample <pid> <seconds> 1 -mayDie -file run.sample
    python3 tools/sample_tree.py run.sample SkiaRaster "rect_memset32" "SkArenaAlloc" ...
    python3 tools/sample_tree.py run.sample SkiaRaster --exclusive 20

--exclusive N prints the N functions with the most exclusive (self) samples in the scope.
"""
import collections
import re
import sys

NODE = re.compile(r"^([ +!:|]*)(\d+) (.*)$")


def parse(path):
    lines = open(path, errors="ignore").read().split("\n")
    start = next(i for i, line in enumerate(lines) if line.startswith("Call graph:"))
    end = next(i for i, line in enumerate(lines) if line.startswith("Total number in stack"))
    nodes = []
    stack = []
    for line in lines[start + 1:end]:
        match = NODE.match(line)
        if not match:
            continue
        depth = len(match.group(1))
        count = int(match.group(2))
        name = re.sub(r"\s+\(in [^)]*\).*$", "", match.group(3))
        while stack and stack[-1][0] >= depth:
            stack.pop()
        ancestors = [frame for _, frame in stack]
        stack.append((depth, name))
        nodes.append((depth, count, name, ancestors))
    return nodes


def scope_total(nodes, scope):
    total = 0
    for _, count, name, ancestors in nodes:
        if scope in name and not any(scope in a for a in ancestors):
            total += count
    return total


def inclusive(nodes, scope, patterns):
    counts = collections.Counter()
    for _, count, name, ancestors in nodes:
        if not any(scope in a for a in ancestors + [name]):
            continue
        for pattern in patterns:
            if re.search(pattern, name) and not any(re.search(pattern, a) for a in ancestors):
                counts[pattern] += count
    return counts


def exclusive(nodes, scope):
    counts = collections.Counter()
    for i, (depth, count, name, ancestors) in enumerate(nodes):
        if not any(scope in a for a in ancestors + [name]):
            continue
        children = 0
        child_depth = None
        j = i + 1
        while j < len(nodes) and nodes[j][0] > depth:
            if child_depth is None:
                child_depth = nodes[j][0]
            if nodes[j][0] == child_depth:
                children += nodes[j][1]
            j += 1
        counts[name] += count - children
    return counts


def main():
    path, scope = sys.argv[1], sys.argv[2]
    nodes = parse(path)
    total = scope_total(nodes, scope)
    print(f"samples in {scope}: {total}")
    if len(sys.argv) == 5 and sys.argv[3] == "--exclusive":
        for name, count in exclusive(nodes, scope).most_common(int(sys.argv[4])):
            print(f"{count:6d} {100 * count / max(total, 1):5.1f}%  {name[:150]}")
        return
    patterns = sys.argv[3:]
    counts = inclusive(nodes, scope, patterns)
    for pattern in patterns:
        print(f"{counts[pattern]:6d} {100 * counts[pattern] / max(total, 1):5.1f}%  {pattern}")


if __name__ == "__main__":
    main()
