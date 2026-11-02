#!/usr/bin/env python3
"""Montage Kindle framebuffer captures (raw 608x800 8bpp) into a row grid PNG.

Usage: montage-frames.py <frames_dir> <out_png> [cols]
Each 608x800 raw is one panel. Greyscale display: value 0 black ... 255 white? --
kindle 8bpp here is inverted so keep it as-is (it renders darker for higher val).
Just present raw values as greyscale.
"""
import sys, os
from PIL import Image

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "scratch/frames"
    out = sys.argv[2] if len(sys.argv) > 2 else "scratch/frames-sheet.png"
    cols = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    files = sorted(f for f in os.listdir(src) if f.endswith(".raw"))
    if not files:
        print("no .raw frames in", src); return 1
    W, H = 608, 800
    rows = (len(files) + cols - 1) // cols
    cell = Image.new("L", (W, H), 255)
    sheet = Image.new("L", (cols * W, rows * H), 255)
    draw_ctx = None
    for i, f in enumerate(files):
        data = open(os.path.join(src, f), "rb").read()
        if len(data) < W * H:
            print(f"short frame {f}: {len(data)}"); continue
        img = Image.frombytes("L", (W, H), data[:W*H])
        r, c = divmod(i, cols)
        sheet.paste(img, (c * W, r * H))
    sheet.save(out)
    print(f"{len(files)} frames -> {out} ({cols}x{rows})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
