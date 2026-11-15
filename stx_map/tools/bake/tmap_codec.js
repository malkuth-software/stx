// The TMAP 2 file writer (the bake's side; tmap_decode.js is the reference
// reader). Format: ../../docs/FORMAT.md.
"use strict";
const M = require("./model.js");
const { Encoder, probs } = require("./rc.js");
const T = require("./turtle.js");

const HILBERT_ORDER = 15;

// The Hilbert index of a start cell (order 15, the inverse of the reader's).
function hilbertIndex(x, y) {
    let d = 0;
    for (let s = 1 << (HILBERT_ORDER - 1); s > 0; s >>= 1) {
        const rx = (x & s) ? 1 : 0, ry = (y & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        if (ry === 0) {
            if (rx === 1) { x = s - 1 - x; y = s - 1 - y; }
            const t = x; x = y; y = t;
        }
        x &= s - 1; y &= s - 1;
    }
    return d;
}

function writeGap(enc, model, gap) {
    const k = M.bitLength(gap);
    M.writeTree(enc, model.k, 0, k, 5);
    const modelled = k - 1 < 2 ? Math.max(0, k - 1) : 2;
    if (modelled > 0) M.writeTree(enc, model.bits, k * 4, (gap >> (k - 1 - modelled)) & ((1 << modelled) - 1), modelled);
    const rest = k - 1 - modelled;
    if (rest > 0) enc.direct(gap & ((1 << rest) - 1), rest);
}

function bitsFor(count) {
    return count <= 1 ? 0 : 32 - Math.clz32(count - 1);
}

// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320).
const CRC_TABLE = (function make_crc_table() {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        table[n] = c >>> 0;
    }
    return table;
})();
function crc32(bytes, end) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < end; i++) c = CRC_TABLE[(c ^ bytes[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
}

// The order a layer's lines are stored in: by the Hilbert index of their
// start cell (stable for equal indices). Returns indices into layer.lines.
function storedOrder(layer) {
    const keyed = layer.lines.map(function keyed_line(line, index) {
        return { index: index, d: hilbertIndex(line.start[0], line.start[1]) };
    });
    keyed.sort(function by_hilbert(a, b) { return a.d - b.d; });
    return keyed.map(function index_of(entry) { return entry.index; });
}

function push32(head, v) {
    head.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255);
}

// layers: [{ name, kind, optional, grid, lines: [{ start: [gx, gy], steps }],
//            order (storedOrder), ties: [{ line (stored index), end (0 start,
//            1 end), layer, vertex, fraction (256ths toward vertex + 1) }] }]
// p: the codec parameters (turtle.makeParams); tolerance: position units.
function encodeMap(layers, p, tolerance) {
    const head = [0x54, 0x4D, 0x41, 0x50, 2, p.mantissaBits, p.uFix & 255, p.uFix >> 8];
    for (let e = 0; e < 16; e++) head.push(p.bins[e]);
    push32(head, T.FP);
    push32(head, tolerance);
    head.push(layers.length);
    const bodies = [];
    const vertexCounts = layers.map(function count_vertices(layer) {
        return layer.lines.reduce(function add(n, l) { return n + l.steps.length + 1; }, 0);
    });
    const detail = { start: 0, steps: 0, ties: 0 };
    layers.forEach(function encode_layer(layer, li) {
        const enc = new Encoder();
        const gaps = { k: probs(64), bits: probs(32 * 4) };
        const steps = new M.StepModel(p);
        let prev = 0;
        for (let i = 0; i < layer.order.length; i++) {
            const line = layer.lines[layer.order[i]];
            const d = hilbertIndex(line.start[0], line.start[1]);
            let before = enc.bits;
            writeGap(enc, gaps, d - prev);
            prev = d;
            detail.start += enc.bits - before;
            before = enc.bits;
            M.encodeSteps(enc, steps, line.steps);
            detail.steps += enc.bits - before;
        }
        const before = enc.bits;
        const lineBits = bitsFor(layer.lines.length);
        for (const tie of layer.ties) {
            enc.direct(tie.line, lineBits);
            enc.direct(tie.end, 1);
            enc.direct(tie.layer, 4);
            enc.direct(tie.vertex, bitsFor(vertexCounts[tie.layer]));
            enc.direct(tie.fraction === 0 ? 0 : 1, 1);
            if (tie.fraction !== 0) enc.direct(tie.fraction, 8);
        }
        detail.ties += enc.bits - before;
        bodies.push(enc.finish());
    });
    layers.forEach(function write_layer_head(layer, li) {
        const name = Buffer.from(layer.name, "ascii");
        let depends = 0;
        for (const tie of layer.ties) if (tie.layer !== li) depends |= 1 << tie.layer;
        head.push(layer.kind, layer.optional ? 1 : 0, name.length, ...name, layer.grid & 255, layer.grid >> 8,
            depends & 255, depends >> 8);
        push32(head, layer.lines.length);
        push32(head, vertexCounts[li]);
        push32(head, layer.ties.length);
        push32(head, bodies[li].length);
    });
    const total = head.length + bodies.reduce(function add(n, b) { return n + b.length; }, 0) + 4;
    const out = new Uint8Array(total);
    out.set(head, 0);
    let at = head.length;
    for (const body of bodies) { out.set(body, at); at += body.length; }
    const crc = crc32(out, at);
    out[at] = crc & 255; out[at + 1] = (crc >>> 8) & 255; out[at + 2] = (crc >>> 16) & 255; out[at + 3] = (crc >>> 24) & 255;
    return { bytes: out, detail: detail };
}

module.exports = { encodeMap, storedOrder, hilbertIndex, crc32, bitsFor };
