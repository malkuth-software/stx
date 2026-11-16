// Adaptive binary range coder (the LZMA construction, with the range kept
// under 2^31 so a decoder works in 32-bit signed integers). Encoder and decoder.
"use strict";

// A probability is two entries: the 15-bit chance of a 0 bit, and how many
// times it has been updated. It moves fast at first (half way, then a
// quarter, ...) and settles at a shift of LIMIT.
const PROB_BITS = 15;
const PROB_INIT = 1 << (PROB_BITS - 1);
const LIMIT = 5;
const TOP = 1 << 23;
const RANGE_INIT = 0x7FFFFFFF;

class Encoder {
    constructor() {
        this.low = 0;            // kept below 2^40 as a double
        this.range = RANGE_INIT;
        this.cache = 0;
        this.cacheSize = 1;
        this.out = [];
        this.bits = 0;           // estimated cost so far, for model studies
    }
    shiftLow() {
        if (this.low < 0xFF000000 || this.low >= 0x100000000) {
            const carry = this.low >= 0x100000000 ? 1 : 0;
            let temp = this.cache;
            do {
                this.out.push((temp + carry) & 0xFF);
                temp = 0xFF;
            } while (--this.cacheSize !== 0);
            this.cache = Math.floor(this.low / 0x1000000) & 0xFF;
        }
        this.cacheSize++;
        this.low = (this.low % 0x1000000) * 256;
    }
    encode(probs, index, bit) {
        const at = index * 2, p = probs[at];
        let shift = probs[at + 1] + 1;
        if (shift < LIMIT) probs[at + 1] = shift; else shift = LIMIT;
        const bound = (this.range >>> PROB_BITS) * p;
        if (bit === 0) {
            this.range = bound;
            probs[at] = p + ((32768 - p) >> shift);
            this.bits -= Math.log2(p / 32768);
        } else {
            this.low += bound;
            this.range = (this.range - bound) >>> 0;
            probs[at] = p - (p >> shift);
            this.bits -= Math.log2(1 - p / 32768);
        }
        while (this.range < TOP) {
            this.range = (this.range << 8) >>> 0;
            this.shiftLow();
        }
    }
    // Raw bits, up to 8 at a time: the range is cut into 2^n equal slots.
    direct(value, count) {
        this.bits += count;
        while (count > 0) {
            const n = count > 8 ? 8 : count;
            count -= n;
            this.range >>>= n;
            this.low += ((value >>> count) & ((1 << n) - 1)) * this.range;
            while (this.range < TOP) {
                this.range = (this.range << 8) >>> 0;
                this.shiftLow();
            }
        }
    }
    finish() {
        for (let i = 0; i < 5; i++) this.shiftLow();
        return Uint8Array.from(this.out.slice(1));
    }
}

class Decoder {
    constructor(bytes, at) {
        this.bytes = bytes;
        this.at = at || 0;
        this.range = RANGE_INIT;
        this.code = 0;
        for (let i = 0; i < 4; i++) this.code = ((this.code << 8) | this.next()) >>> 0;
    }
    next() {
        return this.at < this.bytes.length ? this.bytes[this.at++] : 0;
    }
    decode(probs, index) {
        const at = index * 2, p = probs[at];
        let shift = probs[at + 1] + 1;
        if (shift < LIMIT) probs[at + 1] = shift; else shift = LIMIT;
        const bound = (this.range >>> PROB_BITS) * p;
        let bit;
        if (this.code < bound) {
            this.range = bound;
            probs[at] = p + ((32768 - p) >> shift);
            bit = 0;
        } else {
            this.code = (this.code - bound) >>> 0;
            this.range = (this.range - bound) >>> 0;
            probs[at] = p - (p >> shift);
            bit = 1;
        }
        while (this.range < TOP) {
            this.range = (this.range << 8) >>> 0;
            this.code = ((this.code << 8) | this.next()) >>> 0;
        }
        return bit;
    }
    direct(count) {
        let value = 0;
        while (count > 0) {
            const n = count > 8 ? 8 : count;
            count -= n;
            this.range >>>= n;
            const slot = (this.code / this.range) | 0;
            this.code -= slot * this.range;
            value = (value << n) | slot;
            while (this.range < TOP) {
                this.range = (this.range << 8) >>> 0;
                this.code = ((this.code << 8) | this.next()) >>> 0;
            }
        }
        return value;
    }
}

function probs(count) {
    const p = new Uint16Array(count * 2);
    for (let i = 0; i < count; i++) p[i * 2] = PROB_INIT;
    return p;
}

module.exports = { Encoder, Decoder, probs, PROB_INIT, LIMIT };
