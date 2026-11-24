// Writes ../../tests/data/reference.bin: what the reference decoder
// (tmap_decode.js) gives for data/world_map.tmap with every layer loaded.
// tests/map.cpp decodes the same file in C++ and compares.
//
//   "STXMAPRF", u32 layer count, then per layer:
//   u8 name length, name, u8 kind, u32 lines, u32 vertices,
//   u64 FNV-1a over the layer's starts then its x, y pairs (each value as
//   four little-endian bytes), u32 sample count, then every SAMPLE_EVERY-th
//   vertex's i32 x, y.
//
// usage: node make_test_data.js
"use strict";
const fs = require("fs");
const path = require("path");
const { decodeTmap } = require("./tmap_decode.js");

const SAMPLE_EVERY = 16;
const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK = 0xFFFFFFFFFFFFFFFFn;

function hashLayer(layer) {
    let hash = FNV_OFFSET;
    function feed(value) {
        const v = value >>> 0;
        for (let b = 0; b < 4; b++) {
            hash ^= BigInt((v >>> (8 * b)) & 255);
            hash = (hash * FNV_PRIME) & MASK;
        }
    }
    for (let i = 0; i < layer.starts.length; i++) feed(layer.starts[i]);
    for (let v = 0; v < layer.x.length; v++) { feed(layer.x[v]); feed(layer.y[v]); }
    return hash;
}

function main() {
    const root = path.join(__dirname, "..", "..");
    const bytes = new Uint8Array(fs.readFileSync(path.join(root, "data", "world_map.tmap")));
    const names = [];
    for (let pass = 0; pass < 2; pass++) {
        const all = decodeTmap(bytes, names);
        if (pass === 0) { for (const layer of all.layers) names.push(layer.name); continue; }
        const parts = [];
        const head = Buffer.alloc(12);
        head.write("STXMAPRF", 0, "ascii");
        head.writeUInt32LE(all.layers.length, 8);
        parts.push(head);
        for (const layer of all.layers) {
            const name = Buffer.from(layer.name, "ascii");
            const lines = layer.starts.length - 1;
            const vertices = layer.x.length;
            const samples = Math.floor((vertices + SAMPLE_EVERY - 1) / SAMPLE_EVERY);
            const record = Buffer.alloc(1 + name.length + 1 + 4 + 4 + 8 + 4 + samples * 8);
            let at = 0;
            record.writeUInt8(name.length, at); at += 1;
            name.copy(record, at); at += name.length;
            record.writeUInt8(layer.kind, at); at += 1;
            record.writeUInt32LE(lines, at); at += 4;
            record.writeUInt32LE(vertices, at); at += 4;
            record.writeBigUInt64LE(hashLayer(layer), at); at += 8;
            record.writeUInt32LE(samples, at); at += 4;
            for (let v = 0; v < vertices; v += SAMPLE_EVERY) {
                record.writeInt32LE(layer.x[v], at); at += 4;
                record.writeInt32LE(layer.y[v], at); at += 4;
            }
            parts.push(record);
            console.log(layer.name + " lines " + lines + " vertices " + vertices);
        }
        fs.writeFileSync(path.join(root, "tests", "data", "reference.bin"), Buffer.concat(parts));
    }
}

main();
