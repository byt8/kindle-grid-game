#!/usr/bin/env python3
"""Print a chess screengrab as ASCII art with the board background dropped, so
piece placement can be checked with no image viewer.

    python3 tools/chess_board_ascii.py grab.pgm [--step N]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chess_bg_replace import background_in_ink, geometry, read_pgm  # noqa: E402


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: chess_board_ascii.py grab.pgm [--step N]")
    step = 2
    if "--step" in sys.argv:
        step = int(sys.argv[sys.argv.index("--step") + 1])
    sq, bx, by = geometry()
    w, h, px = read_pgm(sys.argv[1])
    for y in range(by, by + 8 * sq, step):
        line = []
        for x in range(bx, bx + 8 * sq, step):
            ink = px[y * w + x] < 128 and not background_in_ink(x, y, sq, bx, by)
            line.append("#" if ink else ".")
        print("".join(line))


if __name__ == "__main__":
    main()
