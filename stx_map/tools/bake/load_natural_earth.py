#!/usr/bin/env python3
"""Reads the pinned Natural Earth 1:50m files into the bake's source lines.

Input: a directory holding the four GeoJSON files of natural-earth-vector
v5.1.2 (PROVENANCE.md lists their SHA-256 and git blob hashes; every file is
checked before it is parsed). The files are untrusted data: parsed as JSON
only, every geometry checked.

Output: source.json, {"layers": [{"name", "kind", "optional", "source"}]},
each source line a list of [lon, lat] in degrees, unsimplified. Layers:

  land                      rings, ne_50m_land
  lakes                     rings, ne_50m_lakes
  borders_admin0_ne_default lines, ne_50m_admin_0_boundary_lines_land
                            (Natural Earth's default, de facto point of
                            view; optional: loaded only when asked for)

The ocean file is checked but not used: the land rings already bound it.

A ring smaller than 0.04 degrees across, and a ring or line that keeps fewer
than four (rings) or two (lines) points after Douglas-Peucker at 0.02
degrees and 16-bit quantisation, is left out (the selection of the map
codec experiment this package was ported from).

Run with the isolated interpreter:
    python3 -I load_natural_earth.py <natural_earth_dir> <source.json>
Standard library only.
"""

import hashlib
import json
import math
import sys
from pathlib import Path

MAX_SOURCE_BYTES = 20_000_000
FINEST = 0.02

PINNED = {
    "ne_50m_land.geojson": "e874b27a51d146452be360cafb3cc50c86001074a67d534113e6534682f9826b",
    "ne_50m_ocean.geojson": "5b810162af4b20805838cfd766ef1a445c350cf6aafc8b589515daef30033adf",
    "ne_50m_lakes.geojson": "d350b75978b26fe839b797c2c529b2fb8f47fb3983c03f4964e36d5df9378a52",
    "ne_50m_admin_0_boundary_lines_land.geojson": "2faac4f6b34386f3d21b6e018cf151f241f00e5c936d44dd17d7d9bfb147fa48",
}

LAYERS = [
    # name, file, kind (0 rings, 1 lines), optional, accepted geometry types
    ("land", "ne_50m_land.geojson", 0, False, ("Polygon", "MultiPolygon")),
    ("lakes", "ne_50m_lakes.geojson", 0, False, ("Polygon", "MultiPolygon")),
    ("borders_admin0_ne_default", "ne_50m_admin_0_boundary_lines_land.geojson", 1, True,
     ("LineString", "MultiLineString")),
]


class BadData(Exception):
    pass


def read_collection(path):
    size = path.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise BadData(f"{path.name} is {size} bytes, over the limit")
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED[path.name]:
        raise BadData(f"{path.name} has SHA-256 {digest}, not the pinned release's")
    collection = json.loads(data.decode("utf-8"))
    if not isinstance(collection, dict) or collection.get("type") != "FeatureCollection":
        raise BadData(f"{path.name} is not a FeatureCollection")
    features = collection.get("features")
    if not isinstance(features, list):
        raise BadData(f"{path.name} has no feature list")
    return features


def checked_point(point):
    if not isinstance(point, list) or len(point) < 2:
        raise BadData("a coordinate is not a pair")
    lon, lat = point[0], point[1]
    if not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in (lon, lat)):
        raise BadData("a coordinate is not numeric")
    if not (math.isfinite(lon) and math.isfinite(lat)) or abs(lon) > 180.0001 or abs(lat) > 90.0001:
        raise BadData("a coordinate is out of range")
    return (max(-180.0, min(180.0, float(lon))), max(-90.0, min(90.0, float(lat))))


def checked_line(line):
    if not isinstance(line, list):
        raise BadData("a line is not a list")
    return [checked_point(p) for p in line]


def lines_of(features, accepted):
    """Every ring or line of the features, as lists of (lon, lat)."""
    lines = []
    for feature in features:
        if not isinstance(feature, dict):
            raise BadData("a feature is not an object")
        geometry = feature.get("geometry")
        if geometry is None:
            continue
        kind = geometry.get("type") if isinstance(geometry, dict) else None
        if kind not in accepted:
            raise BadData(f"unexpected geometry {kind!r}")
        coordinates = geometry.get("coordinates")
        if not isinstance(coordinates, list):
            raise BadData("geometry without coordinates")
        if kind == "LineString":
            lines.append(checked_line(coordinates))
        elif kind == "MultiLineString" or kind == "Polygon":
            lines.extend(checked_line(line) for line in coordinates)
        elif kind == "MultiPolygon":
            for polygon in coordinates:
                if not isinstance(polygon, list):
                    raise BadData("a polygon is not a list")
                lines.extend(checked_line(ring) for ring in polygon)
    return lines


def segment_distance(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    if length == 0.0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def douglas_peucker(points, tolerance):
    """Iterative, so long coastlines cannot exhaust the recursion limit."""
    count = len(points)
    if count < 3 or tolerance <= 0.0:
        return list(points)
    keep = [False] * count
    keep[0] = keep[-1] = True
    stack = [(0, count - 1)]
    while stack:
        first, last = stack.pop()
        farthest, index = -1.0, -1
        for i in range(first + 1, last):
            d = segment_distance(points[i], points[first], points[last])
            if d > farthest:
                farthest, index = d, i
        if index >= 0 and farthest > tolerance:
            keep[index] = True
            stack.append((first, index))
            stack.append((index, last))
    return [p for p, k in zip(points, keep) if k]


def farthest_from_start(line):
    start = line[0]
    far = 0
    far_distance = -1.0
    for index, point in enumerate(line):
        distance = math.hypot(point[0] - start[0], point[1] - start[1])
        if distance > far_distance:
            far, far_distance = index, distance
    return far


def simplify(line, tolerance, closed):
    if not closed:
        return douglas_peucker(line, tolerance)
    # A closed ring: split at the point farthest from its start, so both
    # halves have distinct ends.
    if len(line) < 4:
        return list(line)
    far = farthest_from_start(line)
    first = douglas_peucker(line[: far + 1], tolerance)
    second = douglas_peucker(line[far:], tolerance)
    return first[:-1] + second


def quantise(line):
    out = []
    for lon, lat in line:
        q = (round((lon + 180.0) / 360.0 * 65535.0), round((lat + 90.0) / 180.0 * 65535.0))
        if not out or out[-1] != q:
            out.append(q)
    return out


def kept(line, closed):
    if closed:
        xs = [p[0] for p in line]
        ys = [p[1] for p in line]
        if max(xs) - min(xs) < FINEST * 2.0 and max(ys) - min(ys) < FINEST * 2.0:
            return False
    q = quantise(simplify(line, FINEST, closed))
    return len(q) >= (4 if closed else 2)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: load_natural_earth.py <natural_earth_dir> <source.json>")
    directory = Path(sys.argv[1])
    read_collection(directory / "ne_50m_ocean.geojson")
    layers = []
    for name, file, kind, optional, accepted in LAYERS:
        lines = lines_of(read_collection(directory / file), accepted)
        closed = kind == 0
        source = [[list(p) for p in line] for line in lines if kept(line, closed)]
        layers.append({"name": name, "kind": kind, "optional": optional, "source": source})
        print(name, "lines", len(source), "points", sum(len(s) for s in source))
    Path(sys.argv[2]).write_text(json.dumps({"layers": layers}))


if __name__ == "__main__":
    main()
