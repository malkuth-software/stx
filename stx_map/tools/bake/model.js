// The step model: which symbols a line's steps turn into and in which
// contexts. One definition serves the adaptive encoder, the adaptive decoder,
// the statistics and the static costs the optimiser plans with.
//
// Per step: a continue flag (context: step number), the length class
// (context: previous class), the length mantissa (context: class), then the
// turn. The first step's turn is its absolute heading, sent raw. Later turns
// send the magnitude's bit length (context: class, previous turn size), a
// same-side flag (does it turn the way the previous turn did; context:
// class, previous size), two modelled bits under the top bit, and the rest raw.
"use strict";
const T = require("./turtle.js");
const { probs } = require("./rc.js");

const SHIFT = 5;
function bitLength(v) { return v === 0 ? 0 : 32 - Math.clz32(v); }
function sizeContext(k, b) { return k === 0 ? 0 : (k <= b - 3 ? 1 : 2); }

class StepModel {
    constructor(p) {
        this.p = p;
        this.end = probs(16);
        this.cls = probs(16 * 16);
        this.mant = probs(16 * 16);
        this.turnK = probs(48 * 16);
        this.turnSide = probs(48);
        this.turnBits = probs(16 * 16 * 4);
    }
}

function writeTree(enc, tree, base, value, bits) {
    let node = 1;
    for (let i = bits - 1; i >= 0; i--) {
        const bit = (value >> i) & 1;
        enc.encode(tree, base + node, bit, SHIFT);
        node = node * 2 + bit;
    }
}
function readTree(dec, tree, base, bits) {
    let node = 1;
    for (let i = 0; i < bits; i++) node = node * 2 + dec.decode(tree, base + node, SHIFT);
    return node - (1 << bits);
}

function encodeSteps(enc, model, steps) {
    const p = model.p;
    let prevE = 0, prevSize = 0, prevSide = 0;
    for (let n = 0; n < steps.length; n++) {
        const s = steps[n];
        enc.encode(model.end, n < 15 ? n : 15, 0, SHIFT);
        writeTree(enc, model.cls, prevE * 16, s.e, 4);
        writeTree(enc, model.mant, s.e * 16, s.m, p.mantissaBits);
        const b = p.bins[s.e];
        if (n === 0) {
            enc.direct(s.turn & ((1 << b) - 1), b);
        } else {
            const mag = Math.abs(s.turn), k = bitLength(mag), ctx = s.e * 3 + prevSize;
            writeTree(enc, model.turnK, ctx * 16, k, 4);
            if (k > 0) {
                const side = s.turn < 0 ? 1 : 0;
                enc.encode(model.turnSide, ctx, side ^ prevSide, SHIFT);
                prevSide = side;
                const modelled = k - 1 < 2 ? k - 1 : 2;
                if (modelled > 0) writeTree(enc, model.turnBits, (s.e * 16 + k) * 4, (mag >> (k - 1 - modelled)) & ((1 << modelled) - 1), modelled);
                const rest = k - 1 - modelled;
                if (rest > 0) enc.direct(mag & ((1 << rest) - 1), rest);
            }
            prevSize = sizeContext(k, b);
        }
        prevE = s.e;
    }
    enc.encode(model.end, steps.length < 15 ? steps.length : 15, 1, SHIFT);
}

// Decodes one line's steps straight into positions. out receives x, y pairs
// (position units) after the start point, which the caller has already pushed.
function decodeSteps(dec, model, x, y, outX, outY, at, limit) {
    const p = model.p;
    let prevE = 0, prevSize = 0, prevSide = 0, heading = 0;
    for (let n = 0; ; n++) {
        if (dec.decode(model.end, n < 15 ? n : 15, SHIFT)) break;
        if (at >= limit) throw new Error("vertex count exceeded");
        const e = readTree(dec, model.cls, prevE * 16, 4);
        const m = readTree(dec, model.mant, e * 16, p.mantissaBits);
        const b = p.bins[e];
        let turn;
        if (n === 0) {
            turn = dec.direct(b);
        } else {
            const ctx = e * 3 + prevSize;
            const k = readTree(dec, model.turnK, ctx * 16, 4);
            turn = 0;
            if (k > 0) {
                const side = dec.decode(model.turnSide, ctx, SHIFT) ^ prevSide;
                prevSide = side;
                let mag = 1;
                const modelled = k - 1 < 2 ? k - 1 : 2;
                if (modelled > 0) mag = (mag << modelled) | readTree(dec, model.turnBits, (e * 16 + k) * 4, modelled);
                const rest = k - 1 - modelled;
                if (rest > 0) mag = (mag << rest) | dec.direct(rest);
                turn = side ? -mag : mag;
            }
            prevSize = sizeContext(k, b);
        }
        heading = (heading + turn * (T.FINE >> b)) & (T.FINE - 1);
        const len = (e === 0 ? m : (p.M + m) << (e - 1)) * p.uFix;
        x += Math.round(len * T.COS[heading] / 16384);
        y += Math.round(len * T.SIN[heading] / 16384);
        outX[at] = x; outY[at] = y; at++;
        prevE = e;
    }
    return at;
}

// ---- statistics and static costs -------------------------------------------

function newCounts() {
    const z = (n, size) => Array.from({ length: n }, () => new Float64Array(size));
    return { cls: z(16, 16), mant: z(16, 16), turn: z(48, 32), end: z(16, 2) };
}
function tally(counts, p, steps) {
    let prevE = 0, prevSize = 0, prevSide = 0;
    steps.forEach((s, n) => {
        counts.end[n < 15 ? n : 15][0]++;
        counts.cls[prevE][s.e]++; counts.mant[s.e][s.m]++;
        if (n > 0) {
            const k = bitLength(Math.abs(s.turn)), side = s.turn < 0 ? 1 : 0;
            counts.turn[s.e * 3 + prevSize][k * 2 + (k > 0 ? side ^ prevSide : 0)]++;
            if (k > 0) prevSide = side;
            prevSize = sizeContext(k, p.bins[s.e]);
        }
        prevE = s.e;
    });
    counts.end[steps.length < 15 ? steps.length : 15][1]++;
}
function makeCosts(p, counts) {
    const table = (row, size, fallback) => {
        const out = new Float64Array(size);
        let total = 0;
        if (row) for (let i = 0; i < size; i++) total += row[i] || 0;
        for (let i = 0; i < size; i++) {
            out[i] = total < 40 ? fallback(i) : -Math.log2(((row[i] || 0) + 0.3) / (total + 0.3 * size));
        }
        return out;
    };
    const c = { cls: [], mant: [], turn: [], end: [] };
    for (let i = 0; i < 16; i++) c.cls.push(table(counts && counts.cls[i], 16, () => 3.5));
    for (let i = 0; i < 16; i++) c.mant.push(table(counts && counts.mant[i], p.M, () => p.mantissaBits));
    for (let i = 0; i < 48; i++) c.turn.push(table(counts && counts.turn[i], 32, (s) => 1.5 + 1.3 * (s >> 1)));
    for (let i = 0; i < 16; i++) c.end.push(table(counts && counts.end[i], 2, (s) => s ? 4.5 : 0.1));
    return c;
}
// Bits for a step from a node state {prevE, prevSize, prevSide, n}.
function turnCost(costs, p, e, state, turn) {
    if (state.n === 0) return p.bins[e];
    const k = bitLength(Math.abs(turn));
    const side = turn < 0 ? 1 : 0;
    return costs.turn[e * 3 + state.prevSize][k * 2 + (k > 0 ? side ^ state.prevSide : 0)] + (k > 1 ? k - 1 : 0);
}

module.exports = { StepModel, encodeSteps, decodeSteps, newCounts, tally, makeCosts, turnCost, bitLength, sizeContext,
    writeTree, readTree, SHIFT };
