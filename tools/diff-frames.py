#!/usr/bin/env python3
"""Analyse Kindle fb captures: for each consecutive pair, report the number of
changed pixels and the bounding box of the change, so the real on-glass update
cadence is visible without eyeballing 50 frames."""
import sys, os
from PIL import Image

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "scratch/frames"
    files = sorted(f for f in os.listdir(src) if f.endswith(".raw"))
    W, H = 608, 800
    prev = None
    for i, f in enumerate(files):
        data = open(os.path.join(src, f), "rb").read()
        if len(data) < W * H:
            print(f"{f}: short"); continue
        img = Image.frombytes("L", (W, H), data[:W*H])
        px = img.load()
        if prev is not None:
            changed = 0
            minx, miny, maxx, maxy = W, H, -1, -1
            for y in range(0, H, 4):
                for x in range(0, W, 4):
                    if abs(px[x, y] - prev[x, y]) > 12:
                        changed += 1
                        if x < minx: minx = x
                        if x > maxx: maxx = x
                        if y < miny: miny = y
                        if y > maxy: maxy = y
            if changed:
                print(f"{f}: changed={changed} bbox=({minx},{miny})-({maxx},{maxy})")
            else:
                print(f"{f}: (no change)")
        prev = px
    return 0

if __name__ == "__main__":
    sys.exit(main())
