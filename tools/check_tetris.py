#!/usr/bin/env python3
"""Check a Tetris picture really is a Tetris picture.

check_pgm.py already proves a dump is a non-blank 600x800 page that changes.
This adds what only Tetris can be checked for: the well is really drawn where
the game says it is, and playing piles blocks into it.

    check_tetris.py open.pgm play.pgm

  open.pgm  the opening screen (empty well)
  play.pgm  the same game after --auto N dropped some pieces

The well geometry must match WELL_X/WELL_Y/CELL in src/tetris.c. The frame is
the 2px black ring that draw_well_frame() puts just outside the playfield.
"""
import sys
from pathlib import Path

W, H = 600, 800

# From src/tetris.c -- keep in step with it.
WELL_X, WELL_Y, CELL = 152, 44, 30
COLS, ROWS = 10, 20

# draw_well_frame paints black from (WELL_X-4, WELL_Y-4) out to (WELL_X+CELLS_W+4,
# WELL_Y+CELLS_H+4), then a paper hole two pixels in. So the black ring sits at
# WELL_X-4 / WELL_X+COLS*CELL+2 across, and WELL_Y-4 / WELL_Y+ROWS*CELL+2 down.
RING_L = WELL_X - 4
RING_R = WELL_X + COLS * CELL + 2
RING_T = WELL_Y - 4
RING_B = WELL_Y + ROWS * CELL + 2


def body(path):
    data = Path(path).read_bytes()
    prefix = b"P5\n%d %d\n255\n" % (W, H)
    if not data.startswith(prefix):
        print(f"check_tetris: {path} is not a {W}x{H} P5", file=sys.stderr)
        return None
    pix = data[len(prefix):]
    if len(pix) != W * H:
        print(f"check_tetris: {path} has {len(pix)} pixels, want {W * H}", file=sys.stderr)
        return None
    return pix


def px(pix, x, y):
    return pix[y * W + x]


def ink(pix, x, y, w, h):
    """How many ink (black) pixels are in this rectangle."""
    return sum(
        1
        for yy in range(y, y + h)
        for xx in range(x, x + w)
        if px(pix, xx, yy) == 0
    )


def main():
    if len(sys.argv) != 3:
        print("usage: check_tetris.py open.pgm play.pgm", file=sys.stderr)
        return 2

    op = body(sys.argv[1])
    pl = body(sys.argv[2])
    if op is None or pl is None:
        return 1

    bad = 0

    # 1. The well frame is a black ring on both pictures. Sample the middle of
    #    each rail so a stray cell cannot make a broken rail look fine.
    cx = WELL_X + COLS * CELL // 2
    cy = WELL_Y + ROWS * CELL // 2
    for name, pix in (("open", op), ("play", pl)):
        rails = {
            "top": (cx, RING_T),
            "bottom": (cx, RING_B),
            "left": (RING_L, cy),
            "right": (RING_R, cy),
        }
        for rail, (x, y) in rails.items():
            if px(pix, x, y) != 0:
                print(f"check_tetris: {name} the {rail} rail of the well is not inked",
                      file=sys.stderr)
                bad = 1

    # 2. Count ink across the whole playfield. Opening is nearly bare; playing
    #    lands pieces and must leave clearly more behind.
    ix, iy = WELL_X, WELL_Y
    iw, ih = COLS * CELL, ROWS * CELL
    open_ink = ink(op, ix, iy, iw, ih)
    play_ink = ink(pl, ix, iy, iw, ih)

    if play_ink <= open_ink:
        print(f"check_tetris: playing added no blocks (open {open_ink} -> play {play_ink})",
              file=sys.stderr)
        bad = 1

    # 3. Landed blocks are solid squares, not speckle. A full cell draws a 26x26
    #    black square with a 2px paper rim, so its centre 20x20 is all ink. Count
    #    such cells in the play picture; there must be at least one.
    filled = 0
    for r in range(ROWS):
        for c in range(COLS):
            cell_ink = ink(pl, WELL_X + c * CELL + 5, WELL_Y + r * CELL + 5, 20, 20)
            if cell_ink > 20 * 20 * 0.8:
                filled += 1
    if filled == 0:
        print("check_tetris: no solid block anywhere in the well after playing",
              file=sys.stderr)
        bad = 1

    if bad:
        return 1

    print(f"well ink {open_ink} -> {play_ink}, {filled} solid blocks")
    print("tetris picture ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
