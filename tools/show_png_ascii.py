#!/usr/bin/env python3
"""show_png_ascii.py -- look at a panel screenshot without a picture viewer.

    python3 tools/show_png_ascii.py kindle-now.png          # coarse, reads fast
    python3 tools/show_png_ascii.py kindle-now.png 10 10    # 10px cells, 10 rows each

Reads a grayscale PNG (the kind fb-shot.sh writes) and prints it as text, so a
device screen can be checked from a terminal or from a session that cannot show
images. Dark blocks are ink; the scale runs " .:-=+*#%@".
"""
import struct
import sys
import zlib

RAMP = " .:-=+*#%@"


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: not a PNG" % path)
    pos, idat, hdr = 8, b"", None
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", chunk[:13])
        elif tag == b"IDAT":
            idat += chunk
        pos += 12 + ln
    w, h, depth, ctype = hdr[0], hdr[1], hdr[2], hdr[3]
    if depth != 8 or ctype not in (0, 6):
        sys.exit("%s: %d-bit type %d is not the 8-bit gray/rgba this reads" % (path, depth, ctype))
    raw = zlib.decompress(idat)
    bpp = 1 if ctype == 0 else 4
    stride = w * bpp
    out, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                left = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((left + prev[x]) // 2)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f != 0:
            sys.exit("%s: unsupported PNG filter %d" % (path, f))
        if bpp == 1:
            out.append(bytes(line))
        else:
            out.append(bytes(line[0::4]))
        prev = line
    return out, w, h


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip().splitlines()[0])
    path = sys.argv[1]
    cell = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    row_step = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    rows, w, h = read_png(path)
    ink = sum(1 for r in rows for b in r if b < 128)
    print("%s %dx%d, %d ink pixels (%.1f%%)" % (path, w, h, ink, 100.0 * ink / (w * h)))
    for y in range(0, h, row_step):
        band = rows[y:y + row_step]
        line = ""
        for x in range(0, w, cell):
            blk = [b for r in band for b in r[x:x + cell]]
            dark = sum(1 for b in blk if b < 128) / len(blk)
            line += RAMP[min(len(RAMP) - 1, int(dark * (len(RAMP) - 1) * 2.2))]
        print("%3d|%s" % (y, line))


if __name__ == "__main__":
    main()
