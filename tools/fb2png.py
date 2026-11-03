#!/usr/bin/env python3
"""Render a Kindle /dev/fb0 dump (608x800x8 grey) to a viewable PNG.

Usage: python3 tools/fb2png.py IN.raw OUT.png [w h]
"""
import struct, sys, zlib

raw = open(sys.argv[1], 'rb').read()
w, h = 600, 800
out = sys.argv[2]
if len(sys.argv) > 4:
    w, h = int(sys.argv[3]), int(sys.argv[4])

rows = []
for y in range(h):
    row = raw[y * 608 : y * 608 + w]
    if len(row) < w:
        row += b'\x00' * (w - len(row))
    rows.append(b'\x00' + row)
rawimg = b''.join(rows)


def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)


png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(rawimg, 9))
png += chunk(b'IEND', b'')
open(out, 'wb').write(png)
print('wrote %s (%dx%d, %d bytes)' % (out, w, h, len(png)))