#!/usr/bin/env python3
"""Check a Rockman picture is the cave, not a blank page.

check_pgm.py already proves the dumps are 600x800 and that the room is not a
copy of the title. This checks the geometry the game claims:

    check_rockman.py title.pgm room.pgm play.pgm

  title.pgm  room 0, no tap
  room.pgm   room 1, after the tap that starts the game
  play.pgm   the same seed after --auto 24
"""
import sys
from pathlib import Path

W, H = 600, 800
ORIGIN_X, ORIGIN_Y = 3, 6
CELL_W, CELL_H = 27, 40


def body(path):
    data = Path(path).read_bytes()
    prefix = b"P5\n%d %d\n255\n" % (W, H)
    if not data.startswith(prefix):
        print(f"check_rockman: {path} is not a {W}x{H} P5", file=sys.stderr)
        return None
    pix = data[len(prefix):]
    if len(pix) != W * H:
        print(f"check_rockman: {path} has {len(pix)} pixels, want {W * H}", file=sys.stderr)
        return None
    return pix


def px(pix, x, y):
    return pix[y * W + x]


def ink_count(pix, x, y, w, h):
    return sum(1 for yy in range(y, y + h) for xx in range(x, x + w) if px(pix, xx, yy) == 0)


def main():
    if len(sys.argv) != 4:
        print("usage: check_rockman.py title.pgm room.pgm play.pgm", file=sys.stderr)
        return 2
    pics = [body(p) for p in sys.argv[1:]]
    if any(p is None for p in pics):
        return 1
    title, room, play = pics
    bad = 0

    # Brick tile row 1 is 0xDF. Cell (0,0) maps that row to y = 3 inside the cell.
    # The leftmost bit is ink, so (ORIGIN_X, ORIGIN_Y+3) is black on every screen.
    for name, pix in (("title", title), ("room", room), ("play", play)):
        if px(pix, ORIGIN_X, ORIGIN_Y + 3) != 0:
            print(f"check_rockman: {name} is missing the brick at the corner", file=sys.stderr)
            bad += 1
        if px(pix, 10, 646) != 0:
            print(f"check_rockman: {name} is missing the LEFT button", file=sys.stderr)
            bad += 1

    # Player starts at screen col 11, row 7.
    px0 = ORIGIN_X + 11 * CELL_W
    py0 = ORIGIN_Y + 7 * CELL_H
    title_ink = ink_count(title, px0, py0, CELL_W, CELL_H)
    room_ink = ink_count(room, px0, py0, CELL_W, CELL_H)
    if room_ink < 200:
        print(f"check_rockman: room 1 player cell has only {room_ink} ink pixels", file=sys.stderr)
        bad += 1
    if title_ink == room_ink and title[py0 * W + px0:py0 * W + px0 + CELL_W] == room[py0 * W + px0:py0 * W + px0 + CELL_W]:
        # The whole cells must differ: the title has no player there.
        same = True
        for y in range(CELL_H):
            a = py0 + y
            if title[a * W + px0:a * W + px0 + CELL_W] != room[a * W + px0:a * W + px0 + CELL_W]:
                same = False
                break
        if same:
            print("check_rockman: room 1 drew no player over the title cell", file=sys.stderr)
            bad += 1

    if room == play:
        print("check_rockman: --auto did not change the room", file=sys.stderr)
        bad += 1

    if bad:
        print(f"check_rockman: {bad} check(s) failed", file=sys.stderr)
        return 1
    print("check_rockman: title, room and play look right")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
