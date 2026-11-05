#!/usr/bin/env python3
"""Turn a Kindle framebuffer dump into a PNG.

Usage: fb_raw_to_png.py RAW OUT.png [STRIDE] [W] [H] [BPP]

The Kindle exposes /dev/fb0 as one byte per pixel, 8 bits of grey, with a row
stride rounded up past the visible width (608 for a 600 px screen). Reading it
as 4bpp yields a mostly-blank picture, so the byte-per-pixel layout is the
default here.
"""
import sys
import zlib
import struct


def write_png(path, w, h, grey):
    raw = b"".join(b"\x00" + grey[y * w:(y + 1) * w] for y in range(h))

    def chunk(tag, data):
        c = tag + data
        return (struct.pack(">I", len(data)) + c
                + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    raw_path, out = sys.argv[1], sys.argv[2]
    stride = int(sys.argv[3]) if len(sys.argv) > 3 else 608
    w = int(sys.argv[4]) if len(sys.argv) > 4 else 600
    h = int(sys.argv[5]) if len(sys.argv) > 5 else 800

    data = open(raw_path, "rb").read()
    need = stride * h
    if len(data) < need:
        raise SystemExit("%s: %d bytes, need %d for %dx%d stride %d"
                         % (raw_path, len(data), need, w, h, stride))

    grey = bytearray()
    for y in range(h):
        grey += data[y * stride:y * stride + w]

    write_png(out, w, h, bytes(grey))
    dark = sum(1 for v in grey if v < 128)
    print("wrote %s (%dx%d, %.1f%% ink)" % (out, w, h, 100.0 * dark / (w * h)))


if __name__ == "__main__":
    main()
