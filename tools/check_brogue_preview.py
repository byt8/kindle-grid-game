#!/usr/bin/env python3
"""Check the brogue host preview frames.

The framebuffer is still 600x800. The picture is landscape, rotated 90
degrees clockwise: the floating keyboard is the left 112 px (logical bottom),
and the game is the rest. Exits non-zero on failure.
"""
import glob
import sys

pgms = sorted(glob.glob("/tmp/kfb-frames/frame_*.pgm"))
if len(pgms) < 12:
    sys.exit("brogue: expected >= 12 frames, got %d" % len(pgms))
d = open(pgms[0], "rb").read()
i = d.find(b"\n")
j = d.find(b"\n", i + 1)
w, h = map(int, d[i + 1:j].split())
if (w, h) != (600, 800):
    sys.exit("brogue: frame size %dx%d, want 600x800" % (w, h))
# header is "P5\n600 800\n255\n" which is 15 bytes when the size line is "600 800"
pix = d[j + 1:]
# skip the maxval line
nl = pix.find(b"\n")
pix = pix[nl + 1:]
if len(pix) != 600 * 800:
    sys.exit("brogue: pixel count %d, want %d" % (len(pix), 600 * 800))
kb = sum(1 for y in range(800) for x in range(112) if pix[y * 600 + x] != 255)
game = sum(1 for y in range(800) for x in range(112, 600) if pix[y * 600 + x] < 250)
if game < 1000:
    sys.exit("brogue: game window looks blank (%d dark px)" % game)
if kb < 20000:
    sys.exit("brogue: floating keyboard missing (%d non-paper px)" % kb)
print("brogue: host preview ok (%d frames, %d dark map px, %d keyboard px)" % (len(pgms), game, kb))
