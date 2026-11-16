// Independent error measure: symmetric Hausdorff distance in degrees between a
// source line and a decoded line, both as polylines. Uses a bucket grid, so a
// distance over CELL is only reported as "at least CELL".
"use strict";
const CELL = 0.25;

function segmentDistance(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const len = dx * dx + dy * dy;
    let t = len === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

function directed(from, to) {
    // max over sample points of `from` (vertices and points every 0.004
    // degrees along its segments) of the distance to polyline `to`.
    const grid = new Map();
    for (let i = 0; i + 1 < to.length; i++) {
        const ax = to[i][0], ay = to[i][1], bx = to[i + 1][0], by = to[i + 1][1];
        const x0 = Math.floor(Math.min(ax, bx) / CELL) - 1, x1 = Math.floor(Math.max(ax, bx) / CELL) + 1;
        const y0 = Math.floor(Math.min(ay, by) / CELL) - 1, y1 = Math.floor(Math.max(ay, by) / CELL) + 1;
        for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1; y++) {
            const key = x * 100003 + y;
            let list = grid.get(key);
            if (!list) { list = []; grid.set(key, list); }
            list.push(i);
        }
    }
    let worst = 0;
    const probe = (px, py) => {
        const list = grid.get(Math.floor(px / CELL) * 100003 + Math.floor(py / CELL));
        let best = CELL;
        if (list) for (const i of list) {
            const d = segmentDistance(px, py, to[i][0], to[i][1], to[i + 1][0], to[i + 1][1]);
            if (d < best) best = d;
        }
        if (best > worst) worst = best;
    };
    for (let i = 0; i < from.length; i++) {
        probe(from[i][0], from[i][1]);
        if (i + 1 < from.length) {
            const dx = from[i + 1][0] - from[i][0], dy = from[i + 1][1] - from[i][1];
            const k = Math.floor(Math.hypot(dx, dy) / 0.004);
            for (let s = 1; s <= k; s++) probe(from[i][0] + dx * s / (k + 1), from[i][1] + dy * s / (k + 1));
        }
    }
    return worst;
}

function closedCopy(line, closed) {
    if (!closed) return line;
    const a = line[0], b = line[line.length - 1];
    return (a[0] === b[0] && a[1] === b[1]) ? line : line.concat([a]);
}

function hausdorff(source, decoded, closed) {
    const s = closedCopy(source, closed), q = closedCopy(decoded, closed);
    return [directed(s, q), directed(q, s)];
}

module.exports = { hausdorff };

if (require.main === module) {
    // Self-check against the baked level (python verify.py gave 0.0227 max).
    const map = JSON.parse(require("fs").readFileSync(__dirname + "/cache/map.json"));
    for (const layer of map.layers) {
        let f = 0, b = 0;
        layer.source.forEach((s, i) => {
            const q = layer.finest[i].map(([x, y]) => [x / 65535 * 360 - 180, y / 65535 * 180 - 90]);
            const [a, c] = hausdorff(s, q, layer.kind === 0);
            f = Math.max(f, a); b = Math.max(b, c);
        });
        console.log(layer.name, "source->baked", f.toFixed(5), "baked->source", b.toFixed(5));
    }
}
