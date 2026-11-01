#!/usr/bin/env python3
"""Coarse ASCII preview of the Kindle screens/*.png to confirm art rendered."""
import glob
import struct
import zlib


def read(path):
    d = open(path, 'rb').read()
    pos, idat, w, h = 8, [], None, None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        tag = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if tag == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break
    raw = zlib.decompress(b''.join(idat))
    stride = w + 1
    rows = []
    for y in range(h):
        line = bytearray(raw[y * stride + 1:(y + 1) * stride])
        prev = rows[-1] if rows else None
        ft = raw[y * stride]
        if ft == 1:
            for i in range(1, w):
                line[i] = (line[i] + line[i - 1]) & 255
        elif ft == 2:
            for i in range(w):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(w):
                line[i] = (line[i] + ((line[i - 1] if i else 0) + prev[i]) // 2) & 255
        elif ft == 4:
            for i in range(w):
                a = line[i - 1] if i else 0
                b = prev[i]
                c = prev[i - 1] if i else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        rows.append(line)
    return w, h, rows


def main():
    for p in sorted(glob.glob('screens/*.png')):
        w, h, rows = read(p)
        tot = w * h
        black = sum(1 for r in rows for v in r if v < 32)
        white = sum(1 for r in rows for v in r if v > 223)
        print('=' * 14, p, f'{w}x{h}')
        print(f'  black%={black * 100 // tot} white%={white * 100 // tot}')
        chars = ' .:-=+*#%@'
        cw, ch = 40, 20
        for gy in range(ch):
            line = ''
            for gx in range(cw):
                s = n = 0
                for y in range(gy * h // ch, (gy + 1) * h // ch):
                    for x in range(gx * w // cw, (gx + 1) * w // cw):
                        s += rows[y][x]
                        n += 1
                line += chars[min(9, (255 - s // n) * 10 // 256)]
            print('   ' + line)


if __name__ == '__main__':
    main()
