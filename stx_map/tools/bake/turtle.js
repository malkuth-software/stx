// Polar ("turtle") coding of the map within a tolerance.
//
// Each line is unrolled into steps. A step is a length and a turn from the
// previous heading; the decoder lays the steps end to end. The encoder picks
// each step so the decoded segment stays within E of the source and reaches
// as far along it as possible. Positions are integers (FP units per degree),
// so encoder and decoder agree exactly and nothing drifts.
"use strict";
const { Encoder, Decoder, probs } = require("./rc.js");

const FP = 32768;                 // position units per degree
const START_GRID = 720;           // start points lie on this grid (0.02197 degrees)
const FINE = 4096;                // headings are multiples of 1/FINE turn
const COS = new Int32Array(FINE), SIN = new Int32Array(FINE);
for (let k = 0; k < FINE; k++) {
    COS[k] = Math.round(Math.cos(2 * Math.PI * k / FINE) * 16384);
    SIN[k] = Math.round(Math.sin(2 * Math.PI * k / FINE) * 16384);
}

function makeParams(options) {
    const p = Object.assign({ E: 0.0226, u: 0.003, arc: 0.012, mantissaBits: 4, minBins: 32 }, options);
    p.M = 1 << p.mantissaBits;
    p.bins = [];                  // log2 of the turn bins, per length class
    for (let e = 0; e < 16; e++) p.bins.push(binsLog2(p, e));
    p.uFix = Math.round(p.u * FP);
    p.u = p.uFix / FP;            // the length unit the decoder really uses
    return p;
}
// Length code c >= 1. c < M: c units. Otherwise class e >= 1, mantissa m:
// (M + m) << (e - 1) units.
function lengthClass(p, c) { return c < p.M ? 0 : 32 - Math.clz32(c >> p.mantissaBits); }
function lengthUnits(p, e, m) { return e === 0 ? m : (p.M + m) << (e - 1); }
function binsLog2(p, e) {
    const typical = (e === 0 ? p.M / 2 : 1.5 * p.M * (1 << (e - 1))) * p.u;
    const want = 2 * Math.PI * typical / p.arc;
    let b = Math.round(Math.log2(Math.max(want, p.minBins)) + (p.binsBias || 0));
    return Math.max(2, Math.min(12, b));
}
function stepVector(p, e, m, heading) {
    const len = lengthUnits(p, e, m) * p.uFix;
    return [Math.round(len * COS[heading] / 16384), Math.round(len * SIN[heading] / 16384)];
}

// ---- geometry -------------------------------------------------------------

// Interval of L where P + L d is within E of segment a-b; null if none.
function stadiumInterval(px, py, dx, dy, ax, ay, bx, by, E) {
    let lo = Infinity, hi = -Infinity;
    const dd = dx * dx + dy * dy;
    const disc = (cx, cy) => {
        const fx = cx - px, fy = cy - py;
        const tau = (fx * dx + fy * dy) / dd;
        const h2 = fx * fx + fy * fy - tau * tau * dd;
        if (h2 > E * E) return;
        const w = Math.sqrt((E * E - h2) / dd);
        if (tau - w < lo) lo = tau - w;
        if (tau + w > hi) hi = tau + w;
    };
    disc(ax, ay); disc(bx, by);
    // The rectangle part: points whose projection falls inside a-b and whose
    // distance to the line a-b is at most E.
    const sx = bx - ax, sy = by - ay, ss = sx * sx + sy * sy;
    if (ss > 0) {
        const sl = Math.sqrt(ss), nx = -sy / sl, ny = sx / sl;
        // along(L) = ((P - a) . s + L d . s) / ss in [0,1]; across(L) = (P - a) . n + L d . n in [-E,E]
        const a0 = ((px - ax) * sx + (py - ay) * sy) / ss, a1 = (dx * sx + dy * sy) / ss;
        const c0 = (px - ax) * nx + (py - ay) * ny, c1 = dx * nx + dy * ny;
        let l = -Infinity, h = Infinity;
        const clip = (v0, v1, min, max) => {
            if (Math.abs(v1) < 1e-15) { if (v0 < min || v0 > max) { l = Infinity; } return; }
            let t0 = (min - v0) / v1, t1 = (max - v0) / v1;
            if (t0 > t1) { const t = t0; t0 = t1; t1 = t; }
            if (t0 > l) l = t0;
            if (t1 < h) h = t1;
        };
        clip(a0, a1, 0, 1); clip(c0, c1, -E, E);
        if (l <= h) { if (l < lo) lo = l; if (h > hi) hi = h; }
    }
    return lo <= hi ? [lo, hi] : null;
}

function segDist2(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    let t = len === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return ex * ex + ey * ey;
}
function anchorParam(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    const t = len === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len;
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

// ---- encoder search -------------------------------------------------------

// S: flat source coordinates in degrees, m: last vertex index. From point P
// (degrees) with first uncovered vertex j, casts a ray along heading and
// calls found(i, lo, hi) for every i such that a step ending at L in
// [lo, hi] covers vertices j..i-1 and ends within E of segment (i-1, i).
function castRay(S, m, px, py, dx, dy, j, E, maxL, found) {
    const dd = dx * dx + dy * dy, E2 = E * E;
    let need = 0, endHi = 0;
    for (let i = j; i <= m; i++) {
        if (i >= 1) {
            const iv = stadiumInterval(px, py, dx, dy, S[2 * i - 2], S[2 * i - 1], S[2 * i], S[2 * i + 1], E);
            if (iv) {
                const lo = Math.max(iv[0], need, 0), hi = Math.min(iv[1], maxL);
                if (lo <= hi) found(i, lo, hi);
            }
        }
        // Can the ray pass vertex i?
        const fx = S[2 * i] - px, fy = S[2 * i + 1] - py;
        const tau = (fx * dx + fy * dy) / dd;
        const f2 = fx * fx + fy * fy;
        if (tau <= 0) { if (f2 > E2) return; endHi = 0; continue; }
        const h2 = f2 - tau * tau * dd;
        if (h2 > E2) return;
        const w = Math.sqrt((E2 - h2) / dd);
        const n = tau - w;
        endHi = tau + w;
        if (n > need) need = n;
        if (need > maxL) return;
    }
    // Every vertex through m is passable: the line can end here, within E
    // of the last vertex.
    if (need <= maxL && need <= endHi) found(m + 1, need, Math.min(maxL, endHi));
}

module.exports = { FP, START_GRID, FINE, COS, SIN, makeParams, lengthClass, lengthUnits, binsLog2, stepVector,
    stadiumInterval, segDist2, anchorParam, castRay };
