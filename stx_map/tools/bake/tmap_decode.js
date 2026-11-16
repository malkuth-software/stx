// TMAP 2 reference decoder, standalone: no imports, runs in a page or in node.
// The format is described in stx_map/docs/FORMAT.md; the C++ decoder
// (stx_map/src/decode.cpp) must give the same positions, which
// make_test_data.js records from this file.
//
// decodeTmap(bytes, wanted) returns
//   { unitsPerDegree, tolerance, layers: [{ name, kind (0 rings, 1 lines),
//     optional, loaded, x: Int32Array, y: Int32Array, starts: Uint32Array }] }
// `wanted` (optional) is a list of layer names; without it every layer that
// is not optional is loaded. A loaded layer also loads the layers its ties
// refer to. x and y are position units from -180 and -90 degrees; starts has
// one entry per line plus the end. Anything that is not a valid TMAP 2 file
// is refused with an Error.
//
// Everything is integer arithmetic. The only floating point is the heading
// table, built from Math.cos and Math.sin and rounded to 14 bits; no entry
// lies within 0.00023 of a unit of a rounding boundary, so any correct
// cosine gives the same table (the C++ decoder carries it as data).
"use strict";

var decodeTmap = (function () {
    var FINE = 4096, LIMIT = 5, TOP = 8388608, MAX_VERTICES = 1 << 22, MAX_COORDINATE = 1 << 28;
    var COS = new Int32Array(FINE), SIN = new Int32Array(FINE);
    for (var k = 0; k < FINE; k++) {
        COS[k] = Math.round(Math.cos(2 * Math.PI * k / FINE) * 16384);
        SIN[k] = Math.round(Math.sin(2 * Math.PI * k / FINE) * 16384);
    }
    var CRC = new Int32Array(256);
    for (var n = 0; n < 256; n++) {
        var c = n;
        for (var j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        CRC[n] = c;
    }
    function crc32(bytes, end) {
        var value = -1;
        for (var i = 0; i < end; i++) value = CRC[(value ^ bytes[i]) & 255] ^ (value >>> 8);
        return (value ^ -1) >>> 0;
    }
    function u16(bytes, at) { return bytes[at] | (bytes[at + 1] << 8); }
    function u32(bytes, at) { return (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0; }
    function bitsFor(count) { return count <= 1 ? 0 : 32 - Math.clz32(count - 1); }

    // A probability is two entries: the 15-bit chance of a 0 bit and its
    // update count. It moves by half, a quarter, ... and settles at 1/32.
    function fresh(count) {
        var p = new Uint16Array(count * 2);
        for (var i = 0; i < count; i++) p[i * 2] = 16384;
        return p;
    }

    // Header and layer records, checked whole before anything is decoded.
    function inspect(bytes) {
        var length = bytes.length;
        if (length < 34 || bytes[0] !== 0x54 || bytes[1] !== 0x4D || bytes[2] !== 0x41 || bytes[3] !== 0x50 || bytes[4] !== 2) {
            throw new Error("not a TMAP 2 file");
        }
        if (crc32(bytes, length - 4) !== u32(bytes, length - 4)) throw new Error("TMAP: check value mismatch");
        var end = length - 4;
        var head = { mantissaBits: bytes[5], lengthUnit: u16(bytes, 6), bins: new Uint8Array(16), layers: [] };
        if (head.mantissaBits > 4) throw new Error("TMAP: bad mantissa bits");
        var at = 8, i;
        for (i = 0; i < 16; i++) {
            head.bins[i] = bytes[at++];
            if (head.bins[i] > 12) throw new Error("TMAP: bad turn bins");
        }
        head.unitsPerDegree = u32(bytes, at);
        head.tolerance = u32(bytes, at + 4);
        at += 8;
        var count = bytes[at++];
        if (count < 1 || count > 16) throw new Error("TMAP: bad layer count");
        var total = 0;
        for (i = 0; i < count; i++) {
            if (at + 3 > end) throw new Error("TMAP: truncated header");
            var kind = bytes[at], flags = bytes[at + 1], nameLength = bytes[at + 2];
            at += 3;
            if (kind > 1 || flags > 1 || nameLength < 1 || nameLength > 32) throw new Error("TMAP: bad layer record");
            if (at + nameLength + 20 > end) throw new Error("TMAP: truncated header");
            var name = "";
            for (var c = 0; c < nameLength; c++) {
                var ch = bytes[at++];
                var fits = (ch >= 0x61 && ch <= 0x7A) || (ch >= 0x30 && ch <= 0x39) || ch === 0x5F;
                if (!fits) throw new Error("TMAP: bad layer name");
                name += String.fromCharCode(ch);
            }
            var layer = {
                name: name, kind: kind, optional: flags === 1, grid: u16(bytes, at), depends: u16(bytes, at + 2),
                lines: u32(bytes, at + 4), vertices: u32(bytes, at + 8), ties: u32(bytes, at + 12), bodyBytes: u32(bytes, at + 16),
            };
            at += 20;
            if (layer.depends >> i) throw new Error("TMAP: a layer depends on itself or a later one");
            if (layer.vertices < layer.lines || layer.vertices > MAX_VERTICES || layer.ties > layer.lines * 2) {
                throw new Error("TMAP: bad counts");
            }
            total += layer.vertices;
            if (total > MAX_VERTICES) throw new Error("TMAP: bad counts");
            head.layers.push(layer);
        }
        for (i = 0; i < count; i++) {
            head.layers[i].bodyAt = at;
            if (head.layers[i].bodyBytes > end - at) throw new Error("TMAP: truncated body");
            at += head.layers[i].bodyBytes;
        }
        if (at !== end) throw new Error("TMAP: trailing bytes");
        return head;
    }

    function decodeLayer(bytes, head, li, out) {
        var layer = head.layers[li];
        var at = layer.bodyAt, stop = layer.bodyAt + layer.bodyBytes;
        var M = 1 << head.mantissaBits, bins = head.bins, lengthUnit = head.lengthUnit;
        var range = 0x7FFFFFFF, code = 0, i;
        for (i = 0; i < 4; i++) code = (code << 8) | (at < stop ? bytes[at++] : 0);
        if (code < 0) throw new Error("TMAP: bad body");

        function bit(probs, index) {
            var slot = index * 2, p = probs[slot];
            var shift = probs[slot + 1] + 1;
            if (shift < LIMIT) probs[slot + 1] = shift; else shift = LIMIT;
            var bound = (range >> 15) * p;
            var value;
            if (code < bound) {
                range = bound;
                probs[slot] = p + ((32768 - p) >> shift);
                value = 0;
            } else {
                code = code - bound;
                range = range - bound;
                probs[slot] = p - (p >> shift);
                value = 1;
            }
            while (range < TOP) {
                range = range << 8;
                code = (code << 8) | (at < stop ? bytes[at++] : 0);
            }
            return value;
        }
        function raw(count) {
            // Up to 8 bits at a time: the range is cut into 2^n equal slots.
            var value = 0;
            while (count > 0) {
                var n = count > 8 ? 8 : count;
                count -= n;
                range = range >> n;
                var slot = (code / range) | 0;
                if (slot >> n) throw new Error("TMAP: bad body");
                code = code - slot * range;
                value = value * (1 << n) + slot;
                while (range < TOP) {
                    range = range << 8;
                    code = (code << 8) | (at < stop ? bytes[at++] : 0);
                }
            }
            return value;
        }
        function tree(probs, base, count) {
            var node = 1;
            for (var n = 0; n < count; n++) node = node * 2 + bit(probs, base + node);
            return node - (1 << count);
        }

        var x = new Int32Array(layer.vertices), y = new Int32Array(layer.vertices);
        var starts = new Uint32Array(layer.lines + 1);
        var gapK = fresh(64), gapBits = fresh(128);
        var endP = fresh(16), clsP = fresh(256), mantP = fresh(256), turnK = fresh(768), turnSide = fresh(48), turnBits = fresh(1024);
        var hilbert = 0, n = 0, k, modelled, rest;
        for (var line = 0; line < layer.lines; line++) {
            // The start: a gap along the Hilbert curve over the start grid.
            k = tree(gapK, 0, 5);
            var gap = 0;
            if (k > 0) {
                gap = 1;
                modelled = k - 1 < 2 ? k - 1 : 2;
                if (modelled > 0) gap = (gap << modelled) | tree(gapBits, k * 4, modelled);
                rest = k - 1 - modelled;
                if (rest > 0) gap = gap * (1 << rest) + raw(rest);
            }
            hilbert += gap;
            if (hilbert >= 1073741824) throw new Error("TMAP: start out of range");
            var gx = 0, gy = 0;
            for (var s = 1; s < 32768; s <<= 1) {
                var q = Math.floor(hilbert / (s * s)) & 3;
                var rx = (q >> 1) & 1, ry = (q ^ rx) & 1;
                if (ry === 0) {
                    if (rx === 1) { gx = s - 1 - gx; gy = s - 1 - gy; }
                    var t = gx; gx = gy; gy = t;
                }
                gx += s * rx; gy += s * ry;
            }
            if (n >= layer.vertices) throw new Error("TMAP: vertex count exceeded");
            starts[line] = n;
            var px = gx * layer.grid, py = gy * layer.grid;
            x[n] = px; y[n] = py; n++;
            // The steps.
            var prevE = 0, prevSize = 0, prevSide = 0, heading = 0;
            for (var step = 0; ; step++) {
                if (bit(endP, step < 15 ? step : 15)) break;
                if (n >= layer.vertices) throw new Error("TMAP: vertex count exceeded");
                var e = tree(clsP, prevE * 16, 4);
                var m = tree(mantP, e * 16, head.mantissaBits);
                var b = bins[e], turn;
                if (step === 0) {
                    turn = raw(b);
                } else {
                    var ctx = e * 3 + prevSize;
                    k = tree(turnK, ctx * 16, 4);
                    turn = 0;
                    if (k > 0) {
                        var side = bit(turnSide, ctx) ^ prevSide;
                        prevSide = side;
                        var mag = 1;
                        modelled = k - 1 < 2 ? k - 1 : 2;
                        if (modelled > 0) mag = (mag << modelled) | tree(turnBits, (e * 16 + k) * 4, modelled);
                        rest = k - 1 - modelled;
                        if (rest > 0) mag = (mag << rest) | raw(rest);
                        turn = side ? -mag : mag;
                    }
                    prevSize = k === 0 ? 0 : (k <= b - 3 ? 1 : 2);
                }
                heading = (heading + turn * (FINE >> b)) & (FINE - 1);
                var len = (e === 0 ? m : (M + m) * (1 << (e - 1))) * lengthUnit;
                px += Math.floor((len * COS[heading] + 8192) / 16384);
                py += Math.floor((len * SIN[heading] + 8192) / 16384);
                if (px > MAX_COORDINATE || px < -MAX_COORDINATE || py > MAX_COORDINATE || py < -MAX_COORDINATE) {
                    throw new Error("TMAP: position out of range");
                }
                x[n] = px; y[n] = py; n++;
                prevE = e;
            }
        }
        if (n !== layer.vertices) throw new Error("TMAP: vertex count mismatch");
        starts[layer.lines] = n;
        var result = { name: layer.name, kind: layer.kind, optional: layer.optional, loaded: true, x: x, y: y, starts: starts };
        out[li] = result;
        // Ties, in stored order: a line end takes the position of a vertex of
        // this layer or of a layer it depends on, or of the point `fraction`
        // 256ths of the way from that vertex to the next.
        var lineBits = bitsFor(layer.lines);
        for (i = 0; i < layer.ties; i++) {
            var tieLine = raw(lineBits), tieEnd = raw(1), target = raw(4);
            if (tieLine >= layer.lines || target > li || (target < li && !((layer.depends >> target) & 1))) {
                throw new Error("TMAP: bad tie");
            }
            var targetLayer = out[target];
            var vertex = raw(bitsFor(head.layers[target].vertices));
            var fraction = 0;
            if (raw(1)) {
                fraction = raw(8);
                if (fraction === 0) throw new Error("TMAP: bad tie");
            }
            var limit = head.layers[target].vertices - (fraction === 0 ? 0 : 1);
            if (vertex >= limit) throw new Error("TMAP: bad tie");
            var tx = targetLayer.x[vertex], ty = targetLayer.y[vertex];
            if (fraction !== 0) {
                tx += Math.floor(((targetLayer.x[vertex + 1] - tx) * fraction + 128) / 256);
                ty += Math.floor(((targetLayer.y[vertex + 1] - ty) * fraction + 128) / 256);
            }
            var atVertex = tieEnd === 0 ? starts[tieLine] : starts[tieLine + 1] - 1;
            x[atVertex] = tx;
            y[atVertex] = ty;
        }
    }

    function decode(bytes, wanted) {
        var head = inspect(bytes);
        var count = head.layers.length, load = new Array(count), i;
        for (i = 0; i < count; i++) {
            load[i] = wanted ? wanted.indexOf(head.layers[i].name) >= 0 : !head.layers[i].optional;
        }
        // Dependencies point only to earlier layers: one pass from the end.
        for (i = count - 1; i >= 0; i--) {
            if (!load[i]) continue;
            for (var d = 0; d < i; d++) if ((head.layers[i].depends >> d) & 1) load[d] = true;
        }
        var out = new Array(count);
        for (i = 0; i < count; i++) {
            if (load[i]) {
                decodeLayer(bytes, head, i, out);
            } else {
                var layer = head.layers[i];
                out[i] = { name: layer.name, kind: layer.kind, optional: layer.optional, loaded: false,
                    x: new Int32Array(0), y: new Int32Array(0), starts: new Uint32Array(0) };
            }
        }
        return { unitsPerDegree: head.unitsPerDegree, tolerance: head.tolerance, layers: out };
    }
    return decode;
})();

if (typeof module !== "undefined") module.exports = { decodeTmap: decodeTmap };
