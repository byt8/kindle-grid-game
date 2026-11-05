#!/usr/bin/env python3
"""Fail unless the second picture shows water in the pipes the first one drew.

The opening picture has the grid full of pipes at their turned angles. The
solved picture runs the water: a wet pipe is drawn wider than a dry one and the
drain is filled in. So the grid area must hold more ink than it did, and both
pictures must have pipes in that area at all.

usage: check_pipes.py open.pgm wet.pgm
"""
import sys
from pathlib import Path

W, H = 600, 800
GRID_X, GRID_Y = 30, 92          # src/pipemania.c GRID_X / GRID_Y
GRID_W, GRID_H = 540, 378        # COLS*CELL / ROWS*CELL


def body(path):
    data = Path(path).read_bytes()
    prefix = b"P5\n%d %d\n255\n" % (W, H)
    if not data.startswith(prefix):
        print(f"check_pipes: {path} is not a {W}x{H} P5", file=sys.stderr)
        return None
    pix = data[len(prefix):]
    if len(pix) != W * H:
        print(f"check_pipes: {path} has {len(pix)} pixels, want {W * H}",
              file=sys.stderr)
        return None
    return pix


def grid_ink(pix):
    """Count black pixels inside the grid frame."""
    n = 0
    for y in range(GRID_Y, GRID_Y + GRID_H):
        row = pix[y * W + GRID_X: y * W + GRID_X + GRID_W]
        n += row.count(0)
    return n


def main():
    if len(sys.argv) != 3:
        print("usage: check_pipes.py open.pgm wet.pgm", file=sys.stderr)
        return 2
    open_pix = body(sys.argv[1])
    wet_pix = body(sys.argv[2])
    if open_pix is None or wet_pix is None:
        return 1

    a = grid_ink(open_pix)
    b = grid_ink(wet_pix)
    print(f"{sys.argv[1]} grid ink {a}")
    print(f"{sys.argv[2]} grid ink {b}")

    if a < 2000:
        print("check_pipes: the opening picture has no pipes in the grid",
              file=sys.stderr)
        return 1
    # The wet pipes are drawn 18 wide instead of 10 and the drain is filled,
    # so the solved picture must be visibly heavier. A tenth is a safe floor.
    if b <= a + a // 10:
        print(f"check_pipes: the water did not show ({b} vs {a})", file=sys.stderr)
        return 1
    print("water visible in the pipes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
