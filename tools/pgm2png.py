#!/usr/bin/env python3
"""Minimal PGM (P5) to PNG converter, for machines with no PIL or ImageMagick.

    python3 tools/pgm2png.py in.pgm out.png
"""
import struct
import sys
import zlib


def read_pgm(path):
    data = open(path, 'rb').read()
    if not data.startswith(b'P5'):
        raise SystemExit(f'{path}: not a binary PGM (P5)')
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            while pos < len(data) and data[pos:pos + 1] != b'\n':
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # single whitespace byte after the maxval
    w, h, maxval = fields
    if maxval != 255:
        raise SystemExit(f'{path}: only 8-bit PGM is supported (maxval {maxval})')
    pix = data[pos:pos + w * h]
    if len(pix) != w * h:
        raise SystemExit(f'{path}: short pixel data ({len(pix)} of {w * h})')
    return w, h, pix


def write_png(path, w, h, pix):
    raw = b''.join(b'\x00' + pix[y * w:(y + 1) * w] for y in range(h))

    def chunk(tag, body):
        return (struct.pack('>I', len(body)) + tag + body
                + struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: pgm2png.py in.pgm out.png')
    write_png(sys.argv[2], *read_pgm(sys.argv[1]))
    print(f'{sys.argv[2]}: written')
