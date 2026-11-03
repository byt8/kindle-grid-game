#!/usr/bin/env python3
"""Rebuild a chess screengrab with the board background replaced by flat paper,
so the piece sprites stand alone and any coordinate drift is obvious.

    python3 tools/chess_bg_replace.py grab.pgm out.png [--zoom N] [--grid]

Input is the PGM the game dumps (--dump / SIGUSR1 shot). The board geometry is
read from src/chess.c's SQ_PX / BOARD_X / BOARD_Y.

A pixel counts as piece ink when it is not what the plain background would draw
there: background is paper except for the Bayer-4 dither inside the dark squares
and the 1px square borders (see ek_dither_rect in the engine).
"""
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "chess.c"

BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
DARK_LEVEL8 = 3          # ek_dither_rect(..., 3) for the dark squares
THR = DARK_LEVEL8 * 2


def geometry():
    text = SRC.read_text()
    def grab(name):
        m = re.search(r"#define\s+%s\s+(-?\d+)" % name, text)
        if not m:
            sys.exit(f"chess_bg_replace: {name} not found in src/chess.c")
        return int(m.group(1))
    return grab("SQ_PX"), grab("BOARD_X"), grab("BOARD_Y")


def read_pgm(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"P5"):
        sys.exit(f"{path}: not a binary PGM (P5)")
    fields, pos = [], 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    w, h, maxval = fields
    if maxval != 255:
        sys.exit(f"{path}: only 8-bit PGM (maxval {maxval})")
    return w, h, data[pos + 1:]


def background_in_ink(x, y, sq, bx, by):
    """Would the plain board draw ink at (x, y)?"""
    for r in range(8):
        for f in range(8):
            x0 = bx + f * sq
            y0 = by + (7 - r) * sq
            if not (x0 <= x < x0 + sq and y0 <= y < y0 + sq):
                continue
            if x == x0 or y == y0 or x == x0 + sq - 1 or y == y0 + sq - 1:
                return True
            if (f + r) & 1:
                return BAYER4[y & 3][x & 3] < THR
            return False
    return False


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def main():
    args = [a for a in sys.argv[1:]]
    if len(args) < 2:
        sys.exit("usage: chess_bg_replace.py in.pgm out.png "
                 "[--zoom N] [--grid] [--board]")
    src, dst = args[0], args[1]
    zoom, grid, board_only = 1, False, False
    if "--zoom" in args:
        zoom = int(args[args.index("--zoom") + 1])
    grid = "--grid" in args
    board_only = "--board" in args

    sq, bx, by = geometry()
    w, h, px = read_pgm(src)

    # piece ink = difference from the plain background
    piece = bytearray(w * h)
    for y in range(h):
        row = y * w
        for x in range(w):
            bg_ink = background_in_ink(x, y, sq, bx, by)
            got_ink = px[row + x] < 128
            if bg_ink != got_ink:
                piece[row + x] = 1

    # report how each piece sits inside its square
    print(f"square {sq}px at ({bx},{by}); offsets in px from the square centre")
    worst = 0
    for r in range(8):
        for f in range(8):
            x0, y0 = bx + f * sq, by + (7 - r) * sq
            xs, ys = [], []
            for y in range(y0 + 2, y0 + sq - 2):
                for x in range(x0 + 2, x0 + sq - 2):
                    if piece[y * w + x]:
                        xs.append(x)
                        ys.append(y)
            if not xs:
                continue
            cx = (min(xs) + max(xs)) / 2
            cy = (min(ys) + max(ys)) / 2
            dx = cx - (x0 + (sq - 1) / 2)
            dy = cy - (y0 + (sq - 1) / 2)
            worst = max(worst, abs(dx), abs(dy))
            if abs(dx) >= 0.5 or abs(dy) >= 0.5:
                print(f"  {chr(97 + f)}{r + 1}: box centre off by dx={dx:+.1f} dy={dy:+.1f} "
                      f"({max(xs)-min(xs)+1}x{max(ys)-min(ys)+1})")
    print(f"worst bounding-box centre offset: {worst:.1f} px")

    # draw: flat paper everywhere, piece ink solid black, optional square grid
    cx0 = bx if board_only else 0
    cy0 = by if board_only else 0
    cw = 8 * sq if board_only else w
    ch = 8 * sq if board_only else h
    zw, zh = cw * zoom, ch * zoom
    ox, oy = cx0 * zoom, cy0 * zoom
    out = []
    for y in range(zh):
        sy = y // zoom + cy0
        line = bytearray([255]) * zw
        for x in range(zw):
            sx = x // zoom + cx0
            line[x] = 0 if piece[sy * w + sx] else 255
        out.append(line)
    if grid:
        def line_x(xx, val):
            if 0 <= xx < zw:
                for y in range(zh):
                    out[y][xx] = val
        def line_y(yy, val):
            if 0 <= yy < zh:
                out[yy][:] = bytes([val]) * zw
        for r in range(9):
            line_y((by + r * sq) * zoom - oy, 128)
        for f in range(9):
            line_x((bx + f * sq) * zoom - ox, 128)
        # a cross at every true square centre: pieces should sit on it
        for r in range(8):
            for f in range(8):
                cx = int((bx + f * sq + (sq - 1) / 2) * zoom) - ox
                cy = int((by + (7 - r) * sq + (sq - 1) / 2) * zoom) - oy
                for d in range(-3, 4):
                    if 0 <= cx + d < zw:
                        out[cy][cx + d] = 200
                    if 0 <= cy + d < zh:
                        out[cy + d][cx] = 200
    write_png(dst, zw, zh, out)
    print(f"written {dst} ({zw}x{zh})")


if __name__ == "__main__":
    main()
