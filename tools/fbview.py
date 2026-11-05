#!/usr/bin/env python3
"""fbview.py IN.raw -- print an ASCII preview of a 608x800x8 Kindle frame.

Handy when a PNG is ambiguous: it shows which quarter of the panel is dark
and in which byte order, so a rotated or offset frame is obvious.
"""
import sys

raw = open(sys.argv[1], 'rb').read()
W, H, ST = 600, 800, 608


def grid(flipx, flipy, step=16, sx=8):
    out = []
    for y in range(0, H, step):
        yy = H - 1 - y if flipy else y
        line = raw[yy * ST:yy * ST + W]
        if flipx:
            line = line[::-1]
        s = ''
        for x in range(0, W, sx):
            n = 0
            for k in range(step):
                yy2 = H - 1 - (y + k) if flipy else y + k
                if yy2 < 0 or yy2 >= H:
                    continue
                row = raw[yy2 * ST:yy2 * ST + W]
                if flipx:
                    row = row[::-1]
                for xx in range(x, min(x + sx, W)):
                    if row[xx] < 64:
                        n += 1
            s += '#' if n > 14 else ('.' if n > 2 else ' ')
        out.append(s)
    return out


dark = sum(1 for b in raw[:W * H] if b < 64)
print('%s dark=%d (%.1f%%)' % (sys.argv[1], dark, 100.0 * dark / (W * H)))
print('--- as stored ---')
for line in grid(False, False):
    print(line)
