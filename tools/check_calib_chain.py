#!/usr/bin/env python3
"""Fail if tapping NEXT cannot walk the calib screens end to end.

Every calib screen must be reachable by touching NEXT. The tuner (screen 1)
draws its own knob rows instead of the shared button strip, and it shipped
with no NEXT at all -- so the chess sprite screen could not be opened on the
glass, which reads as "an old build with no new section" rather than as a
navigation bug. A picture comparison is the only check that catches it: the
missing button compiles, runs, and looks fine on the screen that has it.

Usage: check_calib_chain.py <reached.pgm> <expected.pgm>

The two pictures are the same screen drawn twice, so they differ only in live
counters (MOVE n, GAP/FLASH ms). A one-screen difference in navigation shows
up as tens of percent of pixels, so the threshold is generous on purpose.
"""
import sys
from pathlib import Path

W, H = 600, 800
PREFIX = b"P5\n%d %d\n255\n" % (W, H)
# Live numbers move a handful of glyph cells; a different screen moves the board.
MAX_DIFF_PCT = 12


def body(path):
    data = Path(path).read_bytes()
    if not data.startswith(PREFIX):
        print(f"check_calib_chain: {path} is not a {W}x{H} P5", file=sys.stderr)
        return None
    pix = data[len(PREFIX):]
    if len(pix) != W * H:
        print(f"check_calib_chain: {path} has {len(pix)} pixels, want {W * H}",
              file=sys.stderr)
        return None
    return pix


def main():
    if len(sys.argv) != 3:
        print("usage: check_calib_chain.py reached.pgm expected.pgm", file=sys.stderr)
        return 2
    reached = body(sys.argv[1])
    expected = body(sys.argv[2])
    if reached is None or expected is None:
        return 1
    diff = sum(1 for a, b in zip(reached, expected) if a != b)
    pct = diff * 100 // (W * H)
    if pct >= MAX_DIFF_PCT:
        print(f"check_calib_chain: the tap chain ended on a different screen "
              f"({pct}% of pixels differ, allowed {MAX_DIFF_PCT}%) -- a screen "
              f"is missing its NEXT button", file=sys.stderr)
        return 1
    print(f"calib: tap chain reaches the chess screen ({pct}% pixels differ)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
