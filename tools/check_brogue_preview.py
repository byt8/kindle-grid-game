#!/usr/bin/env python3
"""Check the brogue host preview frames.

The framebuffer is still 600x800. The picture is landscape, rotated 90
degrees clockwise: the floating keyboard is the left 112 px (logical
bottom). The map letters are 3x, so a font stroke in the zoomed band is
at least 3 px wide. Exits non-zero on failure.
"""
import glob
import sys

def load(path):
    d = open(path, "rb").read()
    i = d.find(b"\n")
    j = d.find(b"\n", i + 1)
    w, h = map(int, d[i + 1:j].split())
    pix = d[j + 1:]
    nl = pix.find(b"\n")
    pix = pix[nl + 1:]
    if (w, h) != (600, 800) or len(pix) != 600 * 800:
        sys.exit("brogue: %s is %dx%d, %d bytes" % (path, w, h, len(pix)))
    return pix

def logical(pix, lx, ly):
    # physical (599-ly, lx) -- see kfb_logical_rect for a 1x1 point
    return pix[lx * 600 + (599 - ly)]

def shortest_ink_run(pix, y, x0, x1):
    """Shortest horizontal run of non-paper. None if the row is blank."""
    best = None
    run = 0
    for x in range(x0, x1):
        if logical(pix, x, y) < 250:
            run += 1
        else:
            if run and (best is None or run < best):
                best = run
            run = 0
    if run and (best is None or run < best):
        best = run
    return best

pgms = sorted(glob.glob("/tmp/kfb-frames/frame_*.pgm"))
if len(pgms) < 12:
    sys.exit("brogue: expected >= 12 frames, got %d" % len(pgms))
pix = load(pgms[0])
kb = sum(1 for y in range(800) for x in range(112) if pix[y * 600 + x] != 255)
game = sum(1 for y in range(800) for x in range(112, 600) if pix[y * 600 + x] < 250)
if game < 1000:
    sys.exit("brogue: game window looks blank (%d dark px)" % game)
if kb < 20000:
    sys.exit("brogue: floating keyboard missing (%d non-paper px)" % kb)

# Last frame is in the dungeon (the script has started a game and walked).
# Zoomed map band: logical x 160..783, y 48..479. A 1x font stroke is 1 px,
# so a shortest run of 1 means the zoom did not happen.
last = load(pgms[-1])
found = None
for y in range(48, 480, 8):
    run = shortest_ink_run(last, y, 160, 784)
    if run is None:
        continue
    if found is None or run < found:
        found = run
if found is None:
    sys.exit("brogue: zoomed map band is blank")
if found < 3:
    sys.exit("brogue: map letters are not 3x (shortest ink run %d px)" % found)

# The script walks a few steps from the middle of the page. A scroll would
# shift almost every map pixel. A page flip leaves the page where it is, so
# only the @ (and a tile or two) change.
prev = load(pgms[-2])
changed = 0
total = 0
for y in range(48, 480):
    for x in range(160, 784):
        total += 1
        if logical(prev, x, y) != logical(last, x, y):
            changed += 1
if total == 0 or changed * 100 / total > 15:
    sys.exit("brogue: map scrolled (%d of %d px changed)" % (changed, total))
print("brogue: host preview ok (%d frames, %d dark map px, %d keyboard px, map stroke %d px, page moved %d px)"
      % (len(pgms), game, kb, found, changed))
