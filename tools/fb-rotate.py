#!/usr/bin/env python3
"""rot.py IN.raw OUT.png [--rot180] -- Kindle 608x800x8 frame to a viewable PNG."""
import struct
import sys
import zlib

raw = open(sys.argv[1], 'rb').read()
W, H, ST = 600, 800, 608
flip = '--rot180' in sys.argv
rows = []
for y in range(H):
    yy = H - 1 - y if flip else y
    line = raw[yy * ST:yy * ST + W]
    if flip:
        line = line[::-1]
    rows.append(b'\x00' + line)
img = b''.join(rows)


def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) % (2 ** 32))


png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(img, 9))
png += chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
print('wrote %s' % sys.argv[2])
