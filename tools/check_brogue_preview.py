#!/usr/bin/env python3
"""Check the brogue host preview frames: 600x800 PGM, game window on the
bottom 408 px, control bar on the top 392 px. Exits non-zero on failure."""
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
pix = d[15:]
dark = sum(1 for y in range(800 - 408, 800) for x in range(600) if pix[y * 600 + x] < 250)
if dark < 1000:
    sys.exit("brogue: game window looks blank (%d dark px)" % dark)
bar = sum(1 for y in range(392) for x in range(600) if pix[y * 600 + x] != 255)
if bar < 140000:
    sys.exit("brogue: control bar missing (%d non-paper px)" % bar)
print("brogue: host preview ok (%d frames, %d dark map px)" % (len(pgms), dark))