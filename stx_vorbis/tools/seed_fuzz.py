#!/usr/bin/env python3
"""Extract bounded parser/packet seeds from named, already obtained Ogg files."""
import argparse
import pathlib
import struct


def packets(data: bytes | bytearray) -> list[bytes]:
    result: list[bytes] = []
    offset: int = 0
    partial: bytearray = bytearray()
    while offset + 27 <= len(data):
        if data[offset:offset + 4] != b'OggS':
            break
        segments: int = data[offset + 26]
        header_end: int = offset + 27 + segments
        if header_end > len(data):
            break
        body: int = header_end
        for size in data[offset + 27:header_end]:
            partial.extend(data[body:body + size])
            body += size
            if size < 255:
                result.append(bytes(partial))
                partial.clear()
        offset = body
    return result


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('destination', type=pathlib.Path)
    parser.add_argument('files', nargs='+', type=pathlib.Path)
    args: argparse.Namespace = parser.parse_args()
    for stage in ('decoder', 'ogg', 'bits', 'huffman', 'setup', 'packet'):
        (args.destination / stage).mkdir(parents=True, exist_ok=True)
    (args.destination / 'bits' / 'boundaries').write_bytes(bytes(range(256)))
    (args.destination / 'huffman' / 'entry-order').write_bytes(bytes([7, 2, 4, 4, 4, 4, 2, 3, 3, 0, 255, 128]))
    for file in args.files:
        data: bytes = file.read_bytes()
        for stage in ('decoder', 'ogg'):
            (args.destination / stage / file.name).write_bytes(data)
        content: list[bytes] = packets(data)
        if len(content) < 4 or len(content[2]) > 65535:
            continue
        identification: bytes = content[0]
        channels: int = identification[11]
        blocks: int = identification[28]
        encoded_blocks: int = ((blocks & 15) - 6) | (((blocks >> 4) - 6) << 4)
        prefix: bytes = struct.pack('<HBB', len(content[2]), channels - 1, encoded_blocks)
        context: bytes = prefix + content[2]
        (args.destination / 'setup' / file.name).write_bytes(context)
        for index, packet in enumerate(content[3:11]):
            (args.destination / 'packet' / f'{file.stem}-{index}').write_bytes(context + packet)


if __name__ == '__main__':
    main()
