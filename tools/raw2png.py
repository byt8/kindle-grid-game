#!/usr/bin/env python3
"""Convert Kindle framebuffer .raw captures (600x800, 8bpp, stride 608) to PNGs.
Usage: raw2png.py <file.raw> [file.png]
Prints ink-row stats for the frame."""
import struct, sys, zlib

STRIDE, W, H = 608, 600, 800

def raw_to_png(d, out):
    rows = [sum(1 for b in d[y*STRIDE:y*STRIDE+W] if b < 0x80) for y in range(H)]
    non = [y for y, i in enumerate(rows) if i > 2]
    out_b = bytearray()
    for y in range(H):
        out_b.append(0)
        out_b.extend(d[y*STRIDE:y*STRIDE+W])
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag+data) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(out_b), 9))
    png += chunk(b'IEND', b'')
    open(out, 'wb').write(png)
    return (len(non), non[0] if non else None, non[-1] if non else None)

if __name__ == '__main__':
    raw, out = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].replace('.raw', '.png')
    d = open(raw, 'rb').read()
    if len(d) < STRIDE * H:
        sys.exit("short read: %d bytes" % len(d))
    info = raw_to_png(d, out)
    print("%s -> %s ink=%s first=%s last=%s" % (raw, out, info[0], info[1], info[2]))