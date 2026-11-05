#!/usr/bin/env python3
"""crop_pgm.py IN.pgm OUT.png X Y W H [SCALE] -- crop a PGM render and zoom it.

Used to look closely at a small part of a host render (a button, a chip) when
the whole 600x800 page is too small to judge by eye.
"""
import sys
import zlib
import struct


def load(path):
    data = open(path, 'rb').read()
    fields = data.split(b'\n', 3)
    if fields[0] != b'P5':
        sys.exit('%s: not a P5 PGM' % path)
    w, h = map(int, fields[1].split())
    return w, h, fields[3]


def chunk(tag, payload):
    body = tag + payload
    return (struct.pack('>I', len(payload)) + body +
            struct.pack('>I', zlib.crc32(body) & 0xffffffff))


def main():
    if len(sys.argv) < 7:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    x0, y0, cw, ch = (int(v) for v in sys.argv[3:7])
    scale = int(sys.argv[7]) if len(sys.argv) > 7 else 4
    w, h, px = load(src)
    if x0 < 0 or y0 < 0 or x0 + cw > w or y0 + ch > h:
        sys.exit('crop %d,%d %dx%d does not fit in %dx%d' % (x0, y0, cw, ch, w, h))
    rows = []
    for y in range(y0, y0 + ch):
        row = bytearray()
        for x in range(x0, x0 + cw):
            row.extend(bytes([px[y * w + x]]) * scale)
        rows.append(bytes(row) * scale)
    raw = b''.join(b'\x00' + r for r in rows)
    out = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', cw * scale, ch * scale, 8, 0, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw)) +
           chunk(b'IEND', b''))
    open(dst, 'wb').write(out)
    print('%s: %dx%d crop of %d,%d shown at %dx' % (dst, cw, ch, x0, y0, scale))


if __name__ == '__main__':
    main()
