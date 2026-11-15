// Rate-optimising turtle encoder: a search over step chains for the fewest
// coded bits, not the fewest steps. Nodes are decoded vertices; they are
// expanded in order of progress along the source, and a node is dropped when
// another node is at least as far along for fewer bits.
"use strict";
const T = require("./turtle.js");

const M = require("./model.js");

class Heap {
    constructor() { this.a = []; }
    push(n) {
        const a = this.a; let i = a.length; a.push(n);
        while (i > 0) { const q = (i - 1) >> 1; if (a[q].progress <= n.progress) break; a[i] = a[q]; i = q; }
        a[i] = n;
    }
    pop() {
        const a = this.a, top = a[0], last = a.pop();
        if (a.length) {
            let i = 0;
            for (;;) {
                let k = 2 * i + 1; if (k >= a.length) break;
                if (k + 1 < a.length && a[k + 1].progress < a[k].progress) k++;
                if (a[k].progress >= last.progress) break;
                a[i] = a[k]; i = k;
            }
            a[i] = last;
        }
        return top;
    }
}

// Pareto staircase of (progress, cost): progress ascending, cost ascending.
class Front {
    constructor() { this.p = []; this.c = []; }
    // Lowest cost among entries at least as far along as progress.
    best(progress) {
        let lo = 0, hi = this.p.length;
        while (lo < hi) { const mid = (lo + hi) >> 1; if (this.p[mid] < progress) lo = mid + 1; else hi = mid; }
        return lo < this.p.length ? this.c[lo] : Infinity;
    }
    add(progress, cost) {
        let lo = 0, hi = this.p.length;
        while (lo < hi) { const mid = (lo + hi) >> 1; if (this.p[mid] < progress) lo = mid + 1; else hi = mid; }
        if (lo < this.p.length && this.c[lo] <= cost) return;
        let first = lo;
        while (first > 0 && this.c[first - 1] >= cost) first--;
        if (lo < this.p.length && this.p[lo] === progress) lo++;
        this.p.splice(first, lo - first, progress); this.c.splice(first, lo - first, cost);
    }
}

// Calls emit(candidate) for every step from node that keeps the tolerance.
// A candidate is a node; candidate.end is true when the step finishes the line.
function expand(S, m, closed, p, E, costs, node, emit) {
    const px = node.x / T.FP - 180, py = node.y / T.FP - 90, j = node.j;
    const bx = S[2 * j] - px, by = S[2 * j + 1] - py, bd = Math.hypot(bx, by);
    const bearing = Math.atan2(by, bx);
    const window = bd > E ? Math.asin(E / bd) : Math.PI;
    for (let e = 0; e <= 14; e++) {
        const minL = T.lengthUnits(p, e, e === 0 ? 1 : 0) * p.u, maxL = T.lengthUnits(p, e, p.M - 1) * p.u;
        if (e > 0 && minL > 60) break;
        const b = p.bins[e], bins = 1 << b, stride = T.FINE >> b;
        const classBits = costs.cls[node.prevE][e] + costs.end[node.n < 15 ? node.n : 15][0];
        const centre = Math.round(((bearing / (2 * Math.PI) * T.FINE) - node.heading) / stride);
        const half = window >= Math.PI ? bins >> 1 : Math.ceil(window / (2 * Math.PI) * bins) + 1;
        const scale = e === 0 ? p.u : p.u * (1 << (e - 1)), base = e === 0 ? 0 : p.M;
        for (let o = -half; o <= half; o++) {
            let turn = centre + o;
            turn = ((turn % bins) + bins) % bins;
            if (turn >= bins >> 1) turn -= bins;
            const hd = (((node.heading + turn * stride) % T.FINE) + T.FINE) % T.FINE;
            const dx = T.COS[hd] / 16384, dy = T.SIN[hd] / 16384;
            const turnBits = M.turnCost(costs, p, e, node, turn);
            const k = M.bitLength(Math.abs(turn)), side = turn < 0 ? 1 : 0;
            T.castRay(S, m, px, py, dx, dy, j, E, maxL, (i, lo, hi) => {
                if (hi < minL) return;
                if (lo < minL) lo = minL;
                let mLo = Math.ceil(lo / scale - 1e-9) - base, mHi = Math.floor(hi / scale + 1e-9) - base;
                if (mLo < (e === 0 ? 1 : 0)) mLo = e === 0 ? 1 : 0;
                if (mHi > p.M - 1) mHi = p.M - 1;
                if (mLo > mHi) return;
                const end = i === m + 1;
                if (end && closed && node.n + 1 < 2) return;
                let first = mHi, second = mLo;
                if (end) {
                    const fx = S[2 * m] - px, fy = S[2 * m + 1] - py;
                    const tau = (fx * dx + fy * dy) / (dx * dx + dy * dy);
                    let mm = Math.round(tau / scale) - base;
                    first = second = mm < mLo ? mLo : mm > mHi ? mHi : mm;
                }
                for (let mant = first; mant >= second; mant--) {
                    const [sx, sy] = T.stepVector(p, e, mant, hd);
                    const nx = node.x + sx, ny = node.y + sy;
                    const cost = node.cost + classBits + costs.mant[e][mant] + turnBits;
                    let progress = m + 1;
                    if (!end) {
                        progress = (i - 1) + T.anchorParam(nx / T.FP - 180, ny / T.FP - 90,
                            S[2 * i - 2], S[2 * i - 1], S[2 * i], S[2 * i + 1]);
                    }
                    if (progress > node.progress + 1e-9) {
                        emit({ x: nx, y: ny, heading: hd, j: end ? m : i, prevE: e,
                            prevSize: node.n === 0 ? 0 : M.sizeContext(k, b),
                            prevSide: node.n === 0 || k === 0 ? node.prevSide : side,
                            n: node.n + 1, cost, parent: node, step: { e, m: mant, turn }, progress, end });
                    }
                }
            });
        }
    }
}

function rootNode(S, start) {
    const gx = start ? start[0] : Math.round((S[0] + 180) * T.FP / T.START_GRID);
    const gy = start ? start[1] : Math.round((S[1] + 90) * T.FP / T.START_GRID);
    const x = gx * T.START_GRID, y = gy * T.START_GRID;
    return { gx, gy, x, y, heading: 0, j: 1, prevE: 0, prevSize: 0, prevSide: 0, n: 0, cost: 0, parent: null, step: null, end: false,
        progress: T.anchorParam(x / T.FP - 180, y / T.FP - 90, S[0], S[1], S[2], S[3]) };
}
function canClose(S, m, E2, node, root) {
    if (node.n < 2) return false;
    const px = node.x / T.FP - 180, py = node.y / T.FP - 90, x0 = root.x / T.FP - 180, y0 = root.y / T.FP - 90;
    for (let i = node.j; i <= m; i++) if (T.segDist2(S[2 * i], S[2 * i + 1], px, py, x0, y0) > E2) return false;
    return true;
}
function trace(root, last, expanded) {
    const steps = [], points = [];
    for (let n = last; n; n = n.parent) { points.push([n.x, n.y]); if (n.step) steps.push(n.step); }
    steps.reverse(); points.reverse();
    return { start: [root.gx, root.gy], steps, points, expanded, cost: last.cost };
}

// Full search: nodes in progress order, pruned by the staircase with a slack.
function optimiseLine(S, closed, p, E, costs, slack, start) {
    const m = S.length / 2 - 1, E2 = E * E;
    const heap = new Heap(), front = new Front();
    const root = rootNode(S, start);
    heap.push(root);
    let bestEnd = null, expanded = 0;
    while (heap.a.length) {
        const node = heap.pop();
        if (bestEnd && node.cost >= bestEnd.cost) continue;
        if (front.best(node.progress + 1e-9) <= node.cost - slack) continue;
        if (closed && canClose(S, m, E2, node, root)) {
            if (!bestEnd || node.cost < bestEnd.cost) bestEnd = node;
            continue;
        }
        expanded++;
        expand(S, m, closed, p, E, costs, node, (c) => {
            if (bestEnd && c.cost >= bestEnd.cost) return;
            if (c.end) { bestEnd = c; return; }
            if (front.best(c.progress) <= c.cost - slack) return;
            front.add(c.progress, c.cost);
            heap.push(c);
        });
    }
    if (!bestEnd) throw new Error("no path");
    return trace(root, bestEnd, expanded);
}

// Cost-aware greedy: each step maximises progress per bit; with lookahead K,
// the K best first steps are each followed by their best second step and the
// pair with the best progress per bit decides the first step.
function greedyLine(S, closed, p, E, costs, K, start) {
    const m = S.length / 2 - 1, E2 = E * E;
    const root = rootNode(S, start);
    let node = root, expanded = 0;
    const bias = costs.bias || 0;
    const ratio = (from, c) => (c.progress - from.progress) / (c.cost - from.cost + bias);
    for (let guard = 0; guard < 200000; guard++) {
        if (closed && canClose(S, m, E2, node, root)) break;
        const list = [];
        expanded++;
        expand(S, m, closed, p, E, costs, node, (c) => { list.push(c); });
        if (!list.length) throw new Error("no step");
        list.sort((a, b) => ratio(node, b) - ratio(node, a));
        let best = list[0];
        if (K > 1) {
            let bestScore = -1;
            for (let k = 0; k < Math.min(K, list.length); k++) {
                const c = list[k];
                let score;
                if (c.end || (closed && canClose(S, m, E2, c, root))) score = ratio(node, c) * 1.0001;
                else {
                    let top = null;
                    expanded++;
                    expand(S, m, closed, p, E, costs, c, (d) => { if (!top || ratio(node, d) > ratio(node, top)) top = d; });
                    score = top ? ratio(node, top) : 0;
                }
                if (score > bestScore) { bestScore = score; best = c; }
            }
        }
        node = best;
        if (node.end) break;
    }
    return trace(root, node, expanded);
}

module.exports = { optimiseLine, greedyLine };
