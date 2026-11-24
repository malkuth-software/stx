// Bakes the TMAP 2 world map: optimises every line on worker threads,
// iterates the cost tables, ties line ends that met in the source, writes
// the file, decodes it again with tmap_decode.js and checks every line
// against its source (the symmetric Hausdorff distance, check.js).
//
// usage: node bake.js <source.json> <out_dir> ['{"rounds":7,...}']
// The defaults are the shipped map's settings (README.md, "Rebaking").
// Writes <out_dir>/world_map.tmap, counts.json, check_lines.bin and
// bake.txt (one line per round, then the final summary).
"use strict";
const fs = require("fs");
const path = require("path");
const { Worker, isMainThread, parentPort, workerData } = require("worker_threads");
const T = require("./turtle.js");
const M = require("./model.js");
const D = require("./turtle_dp.js");
const check = require("./check.js");
const codec = require("./tmap_codec.js");
const { decodeTmap } = require("./tmap_decode.js");

const DEFAULTS = { arc: 0.045, mantissaBits: 3, rounds: 7, threads: 4 };
// The stated tolerance: every decoded line lies within this of its source
// (744 position units, 0.0227 degrees); a tie that would pass it is dropped.
const TOLERANCE_UNITS = 744;
const TOLERANCE = TOLERANCE_UNITS / T.FP;
// A line end met another line in the source when it lies this close to it
// (degrees, about 11 m): Natural Earth's borders end on the coast within
// 1e-4 degrees or at least 0.01 away.
const MEET = 1e-4;
// A tie onto a ring tries this many of the nearest places on its decoded
// segments.
const RING_CANDIDATES = 8;
// Every CHECK_EVERY-th stored line of each layer goes into check_lines.bin.
const CHECK_EVERY = 8;

function flat(line, closed) {
    const pts = line.slice();
    const a = pts[0], b = pts[pts.length - 1];
    if (closed && (a[0] !== b[0] || a[1] !== b[1])) pts.push(a);
    const S = new Float64Array(pts.length * 2);
    pts.forEach(function store(q, i) { S[2 * i] = q[0]; S[2 * i + 1] = q[1]; });
    return S;
}

function ringArea(pts) {
    let a = 0;
    for (let i = 0; i < pts.length; i++) {
        const p = pts[i], q = pts[(i + 1) % pts.length];
        a += p[0] * q[1] - q[0] * p[1];
    }
    return Math.abs(a) / 2;
}

// One line: the cheapest chain within the tolerance; a ring whose area
// drifts out of 0.6..1.6 of its source is searched again tighter.
function optimiseOne(source, closed, p, costs) {
    const S = flat(source, closed);
    let E = p.E, r, retries = 0;
    for (;;) {
        r = D.optimiseLine(S, closed, p, E, costs, 0);
        if (!closed) break;
        const decoded = r.points.map(function degrees(q) { return [q[0] / T.FP - 180, q[1] / T.FP - 90]; });
        const ratio = ringArea(decoded) / Math.max(1e-12, ringArea(source.slice(0, -1)));
        if ((ratio > 0.6 && ratio < 1.6) || E < p.E / 6) break;
        E *= 0.7;
        retries++;
    }
    return { start: r.start, steps: r.steps, points: r.points, retries: retries };
}

if (!isMainThread) {
    const { options, counts, jobs, sourcePath } = workerData;
    const p = T.makeParams(options);
    const costs = M.makeCosts(p, counts);
    const map = JSON.parse(fs.readFileSync(sourcePath));
    const results = [];
    for (const job of jobs) {
        const layer = map.layers[job[0]];
        results.push([job[0], job[1], optimiseOne(layer.source[job[1]], layer.kind === 0, p, costs)]);
    }
    parentPort.postMessage(results);
} else {
    main();
}

function segmentDistance(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    let t = len === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Where each line end of each lines layer met another line in the source:
// { layer, line, end, onto: "rings" | "end" | "vertex", target layer and
// source line, key (the meeting point) }.
function findMeetings(map) {
    const CELL = 0.1;
    const segments = new Map();
    const vertices = new Map();
    map.layers.forEach(function index_layer(layer, li) {
        layer.source.forEach(function index_line(s, n) {
            s.forEach(function index_vertex(q, v) {
                const key = q[0] + "," + q[1];
                if (!vertices.has(key)) vertices.set(key, []);
                vertices.get(key).push([li, n, v, s.length]);
            });
            if (layer.kind !== 0) return;
            for (let i = 0; i + 1 < s.length; i++) {
                const a = s[i], b = s[i + 1];
                for (let x = Math.floor(Math.min(a[0], b[0]) / CELL); x <= Math.floor(Math.max(a[0], b[0]) / CELL); x++) {
                    for (let y = Math.floor(Math.min(a[1], b[1]) / CELL); y <= Math.floor(Math.max(a[1], b[1]) / CELL); y++) {
                        const key = x + "," + y;
                        if (!segments.has(key)) segments.set(key, []);
                        segments.get(key).push([li, n, i]);
                    }
                }
            }
        });
    });
    const meetings = [];
    map.layers.forEach(function meet_layer(layer, li) {
        if (layer.kind !== 1) return;
        layer.source.forEach(function meet_line(s, n) {
            for (let end = 0; end < 2; end++) {
                const q = s[end === 0 ? 0 : s.length - 1];
                let best = MEET, ring = null;
                for (let x = -1; x <= 1; x++) for (let y = -1; y <= 1; y++) {
                    const list = segments.get((Math.floor(q[0] / CELL) + x) + "," + (Math.floor(q[1] / CELL) + y));
                    if (!list) continue;
                    for (const [tl, tn, i] of list) {
                        const r = map.layers[tl].source[tn];
                        const d = segmentDistance(q[0], q[1], r[i][0], r[i][1], r[i + 1][0], r[i + 1][1]);
                        if (d <= best) { best = d; ring = [tl, tn]; }
                    }
                }
                const key = q[0] + "," + q[1];
                if (ring !== null) {
                    meetings.push({ layer: li, line: n, end: end, onto: "rings", targetLayer: ring[0], targetLine: ring[1], key: key, point: q });
                    continue;
                }
                const others = (vertices.get(key) || []).filter(function other(entry) {
                    return entry[0] === li && entry[1] !== n;
                });
                if (others.length === 0) continue;
                const atEnd = others.some(function is_end(entry) { return entry[2] === 0 || entry[2] === entry[3] - 1; });
                const first = others[0];
                meetings.push({ layer: li, line: n, end: end, onto: atEnd ? "end" : "vertex", targetLayer: li,
                    targetLine: first[1], key: key, point: q });
            }
        });
    });
    return meetings;
}

function degreesOf(points) {
    return points.map(function degrees(q) { return [q[0] / T.FP - 180, q[1] / T.FP - 90]; });
}

// The decoded vertices of a planned line nearest a point (degrees), nearest
// first, at most `count`; their indices within the line.
function nearestVertices(points, q, count) {
    const order = [];
    for (let v = 0; v < points.length; v++) {
        order.push([Math.hypot(points[v][0] / T.FP - 180 - q[0], points[v][1] / T.FP - 90 - q[1]), v]);
    }
    order.sort(function nearest_first(a, b) { return a[0] - b[0] || a[1] - b[1]; });
    return order.slice(0, count).map(function index_of(entry) { return entry[1]; });
}

// The point a tie names: vertex v of the points, or the point `fraction`
// 256ths of the way to vertex v + 1, as the decoders compute it.
function tiePoint(points, v, fraction) {
    if (fraction === 0) return [points[v][0], points[v][1]];
    const a = points[v], b = points[v + 1];
    return [a[0] + Math.floor(((b[0] - a[0]) * fraction + 128) / 256), a[1] + Math.floor(((b[1] - a[1]) * fraction + 128) / 256)];
}

// The places on a planned line's segments nearest a point (degrees): per
// segment the nearest 256th, nearest first, at most `count`; each
// [vertex, fraction].
function nearestPlaces(points, q, count) {
    const qx = (q[0] + 180) * T.FP, qy = (q[1] + 90) * T.FP;
    const order = [];
    for (let v = 0; v + 1 < points.length; v++) {
        const ax = points[v][0], ay = points[v][1], dx = points[v + 1][0] - ax, dy = points[v + 1][1] - ay;
        const len = dx * dx + dy * dy;
        let t = len === 0 ? 0 : ((qx - ax) * dx + (qy - ay) * dy) / len;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        let fraction = Math.round(t * 256);
        let vertex = v;
        if (fraction === 256) { vertex = v + 1; fraction = 0; }
        const at = tiePoint(points, vertex, fraction);
        order.push([Math.hypot(at[0] - qx, at[1] - qy), vertex, fraction]);
    }
    order.sort(function nearest_first(a, b) { return a[0] - b[0] || a[1] - b[1] || a[2] - b[2]; });
    return order.slice(0, count).map(function place(entry) { return [entry[1], entry[2]]; });
}

// Plans the ties of every lines layer on the planned points. Returns, per
// layer, the ties in stored order of application, after dropping every tie
// that would put its line past the tolerance; and the counts.
function planTies(map, layers, meetings) {
    const storedOf = layers.map(function inverse(layer) {
        const at = new Array(layer.lines.length);
        layer.order.forEach(function place(index, stored) { at[index] = stored; });
        return at;
    });
    const firstVertex = layers.map(function first_vertices(layer) {
        const first = new Array(layer.order.length);
        let n = 0;
        layer.order.forEach(function count(index, stored) { first[stored] = n; n += layer.lines[index].points.length; });
        return first;
    });
    const result = layers.map(function none() { return []; });
    const stats = { met: meetings.length, rings: 0, ends: 0, vertices: 0, dropped: 0 };
    const byLayer = new Map();
    for (const meeting of meetings) {
        if (!byLayer.has(meeting.layer)) byLayer.set(meeting.layer, []);
        byLayer.get(meeting.layer).push(meeting);
    }
    for (const [li, list] of byLayer) {
        const layer = layers[li];
        // Working copies of the planned points, tied as ties are accepted.
        const points = layer.lines.map(function copy(line) { return line.points.map(function pair(q) { return [q[0], q[1]]; }); });
        const groups = new Map();
        for (const meeting of list) {
            if (!groups.has(meeting.key)) groups.set(meeting.key, []);
            groups.get(meeting.key).push(meeting);
        }
        const accepted = [];
        const tryTie = function try_tie(meeting, targetLayer, targetStored, targetVertexInLine, fraction, targetPoint, quiet) {
            const line = points[meeting.line];
            const v = meeting.end === 0 ? 0 : line.length - 1;
            const kept = line[v];
            line[v] = [targetPoint[0], targetPoint[1]];
            const errors = check.hausdorff(map.layers[li].source[meeting.line], degreesOf(line), false);
            if (Math.max(errors[0], errors[1]) > TOLERANCE) {
                line[v] = kept;
                if (!quiet) stats.dropped++;
                return false;
            }
            accepted.push({
                line: storedOf[li][meeting.line], end: meeting.end, layer: targetLayer,
                vertex: firstVertex[targetLayer][targetStored] + targetVertexInLine, fraction: fraction,
            });
            return true;
        };
        // Onto rings first, then ends onto a group's first end, then onto
        // other lines' inner vertices (whose targets are then final).
        const ringTarget = new Map();
        for (const meeting of list) {
            if (meeting.onto !== "rings") continue;
            const target = layers[meeting.targetLayer];
            const stored = storedOf[meeting.targetLayer][meeting.targetLine];
            const targetPoints = target.lines[meeting.targetLine].points;
            // The nearest place on the ring's decoded segments that keeps
            // the line within the tolerance.
            const candidates = nearestPlaces(targetPoints, meeting.point, RING_CANDIDATES);
            for (let c = 0; c < candidates.length; c++) {
                const v = candidates[c][0], fraction = candidates[c][1];
                const at = tiePoint(targetPoints, v, fraction);
                if (tryTie(meeting, meeting.targetLayer, stored, v, fraction, at, c + 1 < candidates.length)) {
                    stats.rings++;
                    ringTarget.set(meeting.key, [meeting.targetLayer, stored, v, fraction, at]);
                    break;
                }
            }
        }
        for (const [key, group] of groups) {
            const ends = group.filter(function is_end(m) { return m.onto === "end"; });
            if (ends.length === 0) continue;
            if (ringTarget.has(key)) {
                const t = ringTarget.get(key);
                for (const meeting of ends) if (tryTie(meeting, t[0], t[1], t[2], t[3], t[4])) stats.rings++;
                continue;
            }
            // The meeting's first end in stored order keeps its place.
            ends.sort(function by_stored(a, b) { return storedOf[li][a.line] - storedOf[li][b.line]; });
            const first = ends[0];
            const firstLine = points[first.line];
            const fv = first.end === 0 ? 0 : firstLine.length - 1;
            for (let k = 1; k < ends.length; k++) {
                if (tryTie(ends[k], li, storedOf[li][first.line], fv, 0, firstLine[fv])) stats.ends++;
            }
        }
        for (const meeting of list) {
            if (meeting.onto !== "vertex") continue;
            const targetPoints = points[meeting.targetLine];
            const v = nearestVertices(targetPoints, meeting.point, 1)[0];
            if (tryTie(meeting, li, storedOf[li][meeting.targetLine], v, 0, targetPoints[v])) stats.vertices++;
        }
        result[li] = accepted;
    }
    return { ties: result, stats: stats };
}

// check_lines.bin: every CHECK_EVERY-th stored line of each layer, its
// source quantised to position units (round half up):
//   "STXMAPCK", u32 tolerance, u32 entry count, then per entry
//   u8 layer, u8 kind, u32 stored line, u32 point count, i32 x/y pairs.
function writeCheck(file, map, layers) {
    const parts = [];
    let entries = 0;
    layers.forEach(function check_layer(layer, li) {
        layer.order.forEach(function check_line(index, stored) {
            if (stored % CHECK_EVERY !== 0) return;
            const source = map.layers[li].source[index];
            const record = Buffer.alloc(10 + source.length * 8);
            record.writeUInt8(li, 0);
            record.writeUInt8(map.layers[li].kind, 1);
            record.writeUInt32LE(stored, 2);
            record.writeUInt32LE(source.length, 6);
            source.forEach(function quantise(q, v) {
                record.writeInt32LE(Math.floor((q[0] + 180) * T.FP + 0.5), 10 + v * 8);
                record.writeInt32LE(Math.floor((q[1] + 90) * T.FP + 0.5), 14 + v * 8);
            });
            parts.push(record);
            entries++;
        });
    });
    const head = Buffer.alloc(16);
    head.write("STXMAPCK", 0, "ascii");
    head.writeUInt32LE(TOLERANCE_UNITS, 8);
    head.writeUInt32LE(entries, 12);
    fs.writeFileSync(file, Buffer.concat([head].concat(parts)));
    return entries;
}

function main() {
    const sourcePath = path.resolve(process.argv[2] || "");
    const outDir = path.resolve(process.argv[3] || "");
    if (!process.argv[2] || !process.argv[3]) throw new Error("usage: node bake.js <source.json> <out_dir> [options]");
    const options = Object.assign({}, DEFAULTS, process.argv[4] ? JSON.parse(process.argv[4]) : {});
    const p = T.makeParams(options);
    const map = JSON.parse(fs.readFileSync(sourcePath));
    fs.mkdirSync(outDir, { recursive: true });
    const log = [];
    const say = function say(line) { console.log(line); log.push(line); };
    // Share the lines out by size, largest first.
    const all = [];
    map.layers.forEach(function list_layer(layer, li) {
        layer.source.forEach(function list_line(s, n) { all.push([li, n, s.length]); });
    });
    all.sort(function largest_first(a, b) { return b[2] - a[2] || a[0] - b[0] || a[1] - b[1]; });
    const shards = [];
    for (let k = 0; k < options.threads; k++) shards.push({ load: 0, jobs: [] });
    for (const job of all) {
        let s = shards[0];
        for (const other of shards) if (other.load < s.load) s = other;
        s.jobs.push([job[0], job[1]]);
        s.load += job[2] * Math.sqrt(job[2]);
    }
    const plain = function plain(counts) {
        return JSON.parse(JSON.stringify(counts, function arrays(k, v) { return ArrayBuffer.isView(v) ? Array.from(v) : v; }));
    };
    const meetings = findMeetings(map);
    (async function run() {
        let counts = options.countsFrom ? JSON.parse(fs.readFileSync(options.countsFrom)) : null;
        let file = null;
        for (let round = 0; round < options.rounds; round++) {
            const t0 = Date.now();
            const parts = await Promise.all(shards.map(function start_worker(s) {
                return new Promise(function settle(resolve, reject) {
                    const w = new Worker(__filename, { workerData: { options, counts, jobs: s.jobs, sourcePath } });
                    w.on("message", resolve);
                    w.on("error", reject);
                });
            }));
            const layers = map.layers.map(function empty(l) {
                return { name: l.name, kind: l.kind, optional: l.optional, grid: T.START_GRID, lines: new Array(l.source.length), ties: [] };
            });
            const next = M.newCounts();
            let vertices = 0, retries = 0;
            for (const part of parts) for (const [li, n, r] of part) {
                layers[li].lines[n] = r;
                vertices += r.points.length;
                retries += r.retries;
            }
            // Statistics in source order, so the next round's costs do not
            // depend on how the work was shared.
            for (const layer of layers) for (const r of layer.lines) M.tally(next, p, r.steps);
            for (const layer of layers) layer.order = codec.storedOrder(layer);
            const planned = planTies(map, layers, meetings);
            layers.forEach(function set_ties(layer, li) { layer.ties = planned.ties[li]; });
            file = codec.encodeMap(layers, p, TOLERANCE_UNITS);
            // Decode with the reference decoder, compare with the plan, measure.
            const decoded = decodeTmap(file.bytes, map.layers.map(function name(l) { return l.name; }));
            let worst = 0, mismatch = 0;
            layers.forEach(function check_layer(layer, li) {
                const d = decoded.layers[li];
                const tied = new Set(layer.ties.map(function tie_key(t) { return t.line + ":" + t.end; }));
                layer.order.forEach(function check_line(index, stored) {
                    const planned_points = layer.lines[index].points;
                    const first = d.starts[stored], last = d.starts[stored + 1];
                    if (last - first !== planned_points.length) { mismatch++; return; }
                    const line = [];
                    for (let v = first; v < last; v++) {
                        const isTied = (v === first && tied.has(stored + ":0")) || (v === last - 1 && tied.has(stored + ":1"));
                        if (!isTied && (d.x[v] !== planned_points[v - first][0] || d.y[v] !== planned_points[v - first][1])) mismatch++;
                        line.push([d.x[v] / T.FP - 180, d.y[v] / T.FP - 90]);
                    }
                    const errors = check.hausdorff(map.layers[li].source[index], line, layer.kind === 0);
                    worst = Math.max(worst, errors[0], errors[1]);
                });
            });
            const ties = planned.stats;
            say(JSON.stringify(options) + " round " + round + " vertices " + vertices + " bytes " + file.bytes.length +
                " start " + Math.round(file.detail.start / 8) + " steps " + Math.round(file.detail.steps / 8) +
                " ties " + Math.round(file.detail.ties / 8) + " error " + worst.toFixed(5) + " mismatch " + mismatch +
                " retries " + retries + " met " + ties.met + " tied rings " + ties.rings + " ends " + ties.ends +
                " vertices " + ties.vertices + " dropped " + ties.dropped + " sec " + ((Date.now() - t0) / 1000).toFixed(0));
            if (mismatch !== 0 || worst > TOLERANCE) throw new Error("the decoded map does not match the plan or the tolerance");
            counts = plain(next);
            if (round === options.rounds - 1) {
                fs.writeFileSync(path.join(outDir, "world_map.tmap"), file.bytes);
                fs.writeFileSync(path.join(outDir, "counts.json"), JSON.stringify(counts));
                const entries = writeCheck(path.join(outDir, "check_lines.bin"), map, layers);
                say("check lines " + entries);
            }
        }
        fs.writeFileSync(path.join(outDir, "bake.txt"), log.join("\n") + "\n");
    })();
}
