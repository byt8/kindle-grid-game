#!/usr/bin/env python3
"""Per-piece placement report for a chess screengrab.

Counts only ink the board itself would not have drawn (the dark-square Bayer
dither and the 1px square borders are subtracted), then reports each piece's
ink bounding box and ink mass centre versus the square's true centre.

    python3 tools/chess_piece_offsets.py grab.pgm
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chess_bg_replace import background_in_ink, geometry, read_pgm  # noqa: E402


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: chess_piece_offsets.py grab.pgm")
    sq, bx, by = geometry()
    w, h, px = read_pgm(sys.argv[1])
    print(f"{sq}px squares at ({bx},{by}); offsets from the square centre")
    print(f"{'sq':>4} {'bbox':>9} {'bbox dx,dy':>13} {'mass dx,dy':>13}")
    worst_b = worst_m = 0.0
    for r in range(8):
        for f in range(8):
            x0, y0 = bx + f * sq, by + (7 - r) * sq
            xs, ys, mass, mx, my = [], [], 0, 0.0, 0.0
            for y in range(y0, y0 + sq):
                for x in range(x0, x0 + sq):
                    if (px[y * w + x] < 128) == background_in_ink(x, y, sq, bx, by):
                        continue
                    if px[y * w + x] >= 128:
                        continue
                    xs.append(x)
                    ys.append(y)
                    mx += x
                    my += y
                    mass += 1
            if mass < 40:
                continue
            bcx, bcy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            ccx, ccy = x0 + (sq - 1) / 2, y0 + (sq - 1) / 2
            dxb, dyb = bcx - ccx, bcy - ccy
            dxm, dym = mx / mass - ccx, my / mass - ccy
            worst_b = max(worst_b, abs(dxb), abs(dyb))
            worst_m = max(worst_m, abs(dxm), abs(dym))
            print(f"{chr(97 + f)}{r + 1:>3} "
                  f"{max(xs)-min(xs)+1:>3}x{max(ys)-min(ys)+1:<4} "
                  f"{dxb:+6.1f},{dyb:+6.1f} "
                  f"{dxm:+6.1f},{dym:+6.1f}")
    print(f"worst bbox offset {worst_b:.1f} px, worst mass offset {worst_m:.1f} px")


if __name__ == "__main__":
    main()
