#!/usr/bin/env python3
"""Print each baked chess tile's ink bounding box and its centre offset inside
the 52px tile, straight out of src/chess_pieces.h (no rendering involved).

    python3 tools/chess_tile_offsets.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
H = ROOT / "src" / "chess_pieces.h"


def main():
    text = H.read_text()
    tile = int(re.search(r"#define CHESS_TILE (\d+)", text).group(1))
    paper = int(re.search(r"#define CHESS_PAPER (\d+)", text).group(1))
    names = re.findall(r"^\s+(CHS_\w+) = \d+,?$", text, re.M)
    blob = text.split("static const unsigned char ch_px")[1].split("};\n")[0]
    rows = [ln.strip().lstrip("{").split("}")[0]
            for ln in blob.splitlines() if ln.strip().startswith("{")]
    if len(rows) != len(names):
        sys.exit(f"{len(rows)} tiles but {len(names)} names")
    mid = (tile - 1) / 2
    print(f"tile {tile}px, ink centre should be ({mid},{mid})")
    print(f"{'piece':>10} {'bbox':>9} {'dx,dy':>13} {'box':>9}")
    worst = 0.0
    for name, row in zip(names, rows):
        vals = [int(b, 16) for b in row.replace(" ", "").split(",") if b]
        px = []
        for i in range(tile * tile):
            v = (vals[i >> 1] >> ((i & 1) * 4)) & 15
            if v != 0:
                px.append((i % tile, i // tile))
        if not px:
            print(f"{name:>10} empty")
            continue
        xs = [p[0] for p in px]
        ys = [p[1] for p in px]
        cx = (min(xs) + max(xs)) / 2
        cy = (min(ys) + max(ys)) / 2
        dx, dy = cx - mid, cy - mid
        worst = max(worst, abs(dx), abs(dy))
        print(f"{name:>10} {max(xs)-min(xs)+1:>3}x{max(ys)-min(ys)+1:<4} "
              f"{dx:+6.1f},{dy:+6.1f} {min(xs)}..{max(xs)}/{min(ys)}..{max(ys)}")
    print(f"worst tile-centring error {worst:.1f} px")


if __name__ == "__main__":
    main()
