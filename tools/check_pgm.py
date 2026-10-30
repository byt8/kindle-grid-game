#!/usr/bin/env python3
"""Fail if a dump is not a 600x800 black-and-white picture.

The first file is the opening. The second must not be a copy of it,
so a tap that should change the screen cannot pass unnoticed.
"""
import sys
from pathlib import Path

W, H = 600, 800


def body(path):
    data = Path(path).read_bytes()
    prefix = b"P5\n%d %d\n255\n" % (W, H)
    if not data.startswith(prefix):
        print(f"check_pgm: {path} is not a {W}x{H} P5", file=sys.stderr)
        return None
    pix = data[len(prefix):]
    if len(pix) != W * H:
        print(f"check_pgm: {path} has {len(pix)} pixels, want {W * H}", file=sys.stderr)
        return None
    if pix.count(0) < 1000 or pix.count(255) < 1000:
        print(f"check_pgm: {path} is blank", file=sys.stderr)
        return None
    return pix


def main():
    if len(sys.argv) < 3:
        print("usage: check_pgm.py open.pgm changed.pgm [more.pgm...]", file=sys.stderr)
        return 2
    bodies = []
    for path in sys.argv[1:]:
        pix = body(path)
        if pix is None:
            return 1
        bodies.append(pix)
        print(f"{path} ok")
    if bodies[0] == bodies[1]:
        print("check_pgm: the second picture matches the first", file=sys.stderr)
        return 1
    print("pictures differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
