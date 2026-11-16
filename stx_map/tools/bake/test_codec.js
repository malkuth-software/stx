// Round trip of the TMAP 2 writer (tmap_codec.js) through the reference
// reader (tmap_decode.js) on a small synthetic map: two layers, ties with
// and without a fraction, an optional layer and its dependency, and the
// reader's refusals of damaged files. Exits non-zero on the first failure.
//
// usage: node test_codec.js
"use strict";
const T = require("./turtle.js");
const codec = require("./tmap_codec.js");
const { decodeTmap } = require("./tmap_decode.js");

const GRID = 720;
const TOLERANCE = 744;

// A deterministic generator (a 32-bit linear congruential sequence).
function Sequence(seed) { this.state = seed >>> 0; }
Sequence.prototype.next = function next(limit) {
    this.state = (Math.imul(this.state, 1664525) + 1013904223) >>> 0;
    return this.state % limit;
};

function makeLine(p, sequence) {
    const start = [100 + sequence.next(300), 100 + sequence.next(200)];
    const steps = [];
    const count = 3 + sequence.next(5);
    for (let n = 0; n < count; n++) {
        const e = sequence.next(4);
        const m = e === 0 ? 1 + sequence.next(p.M - 1) : sequence.next(p.M);
        const b = p.bins[e];
        const turn = n === 0 ? sequence.next(1 << b) : sequence.next(1 << (b - 1)) - (1 << (b - 2));
        steps.push({ e: e, m: m, turn: turn });
    }
    return { start: start, steps: steps };
}

function pointsOf(p, line) {
    let x = line.start[0] * GRID, y = line.start[1] * GRID, heading = 0;
    const points = [[x, y]];
    for (const s of line.steps) {
        heading = (heading + s.turn * (T.FINE >> p.bins[s.e])) & (T.FINE - 1);
        const v = T.stepVector(p, s.e, s.m, heading);
        x += v[0]; y += v[1];
        points.push([x, y]);
    }
    return points;
}

function expectLayer(p, layer) {
    const x = [], y = [], starts = [0];
    for (const index of layer.order) {
        for (const q of pointsOf(p, layer.lines[index])) { x.push(q[0]); y.push(q[1]); }
        starts.push(x.length);
    }
    return { x: x, y: y, starts: starts };
}

function fail(message) { throw new Error("test_codec: " + message); }

function same(name, got, want) {
    if (got.length !== want.length) fail(name + " length " + got.length + " != " + want.length);
    for (let i = 0; i < want.length; i++) if (got[i] !== want[i]) fail(name + "[" + i + "] " + got[i] + " != " + want[i]);
}

function refuses(name, bytes) {
    try { decodeTmap(bytes, ["land", "borders"]); } catch (error) { return; }
    fail(name + " was not refused");
}

function withCheckValue(bytes) {
    const out = bytes.slice();
    const end = out.length - 4;
    const crc = codec.crc32(out, end);
    out[end] = crc & 255; out[end + 1] = (crc >>> 8) & 255; out[end + 2] = (crc >>> 16) & 255; out[end + 3] = (crc >>> 24) & 255;
    return out;
}

function main() {
    const p = T.makeParams({ arc: 0.045, mantissaBits: 3 });
    const sequence = new Sequence(20261115);
    const land = { name: "land", kind: 0, optional: false, grid: GRID, lines: [], ties: [] };
    const borders = { name: "borders", kind: 1, optional: true, grid: GRID, lines: [], ties: [] };
    for (let i = 0; i < 6; i++) land.lines.push(makeLine(p, sequence));
    for (let i = 0; i < 3; i++) borders.lines.push(makeLine(p, sequence));
    land.order = codec.storedOrder(land);
    borders.order = codec.storedOrder(borders);
    const landWant = expectLayer(p, land);
    const bordersWant = expectLayer(p, borders);
    // Ties: border 0's start onto land vertex 2; border 1's end a quarter of
    // the way from land vertex 5 to 6; border 2's start onto border 0's end.
    borders.ties = [
        { line: 0, end: 0, layer: 0, vertex: 2, fraction: 0 },
        { line: 1, end: 1, layer: 0, vertex: 5, fraction: 64 },
        { line: 2, end: 0, layer: 1, vertex: bordersWant.starts[1] - 1, fraction: 0 }
    ];
    bordersWant.x[0] = landWant.x[2]; bordersWant.y[0] = landWant.y[2];
    const atEnd = bordersWant.starts[2] - 1;
    bordersWant.x[atEnd] = landWant.x[5] + Math.floor(((landWant.x[6] - landWant.x[5]) * 64 + 128) / 256);
    bordersWant.y[atEnd] = landWant.y[5] + Math.floor(((landWant.y[6] - landWant.y[5]) * 64 + 128) / 256);
    const atStart = bordersWant.starts[2];
    bordersWant.x[atStart] = bordersWant.x[bordersWant.starts[1] - 1];
    bordersWant.y[atStart] = bordersWant.y[bordersWant.starts[1] - 1];
    const file = codec.encodeMap([land, borders], p, TOLERANCE).bytes;

    const plain = decodeTmap(file);
    if (!plain.layers[0].loaded || plain.layers[1].loaded) fail("default layers");
    if (plain.tolerance !== TOLERANCE || plain.unitsPerDegree !== T.FP) fail("header values");
    same("land x", plain.layers[0].x, landWant.x);
    same("land y", plain.layers[0].y, landWant.y);
    same("land starts", plain.layers[0].starts, landWant.starts);
    const asked = decodeTmap(file, ["borders"]);
    if (!asked.layers[0].loaded) fail("a layer's dependency was not loaded");
    same("borders x", asked.layers[1].x, bordersWant.x);
    same("borders y", asked.layers[1].y, bordersWant.y);
    same("borders starts", asked.layers[1].starts, bordersWant.starts);

    const flipped = file.slice(); flipped[file.length - 10] ^= 1;
    refuses("a flipped body bit", flipped);
    refuses("a truncated file", file.slice(0, file.length - 1));
    refuses("an empty file", new Uint8Array(0));
    const version = file.slice(); version[4] = 3;
    refuses("version 3", withCheckValue(version));
    const bins = file.slice(); bins[8] = 13;
    refuses("13-bit turn bins", withCheckValue(bins));
    const count = file.slice(); count[32] = 0;
    refuses("no layers", withCheckValue(count));
    // A tie's target vertex is written in bitsFor(vertices) bits; the
    // largest value those bits hold lies past the target's last vertex
    // unless the count is a power of two (the generator's is not).
    const largest = (1 << codec.bitsFor(landWant.x.length)) - 1;
    if (largest < landWant.x.length) fail("the synthetic land layer has a power-of-two vertex count");
    borders.ties = [{ line: 0, end: 0, layer: 0, vertex: largest, fraction: 0 }];
    refuses("a tie past the target's vertices", codec.encodeMap([land, borders], p, TOLERANCE).bytes);
    borders.ties = [{ line: 0, end: 0, layer: 0, vertex: landWant.x.length - 1, fraction: 9 }];
    refuses("a fraction past the target's last vertex", codec.encodeMap([land, borders], p, TOLERANCE).bytes);
    console.log("test_codec: ok, " + file.length + " bytes");
}

main();
