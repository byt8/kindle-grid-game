#!/usr/bin/env python3
"""Build the Kindle screensaver set (20 x 600x800 greyscale PNG) in screens/ss/.

The Kindle slideshow ships 20 full-screen pictures
(/usr/share/blanket/screensaver/bg_xsmall_ssNN.png on the read-only root).
Each is a 600x800 PNG; the stock files are palette (colour type 3), the
greyscale form (colour type 0) that make_splash writes works the same through
cairo and is what the boot files already use.

The 5 new AI pieces are dense ink; the slideshow shows the art as-is (no boot
labels are painted over a screensaver, so nothing needs washing out). Each piece
fills four slots -- full frame, zoomed centre, zoomed top, zoomed bottom -- so
it varies while staying one style.

Usage:
  python3 tools/make_screensaver_screens.py   # rebuild screens/ss00..ss19.png
"""
import glob
import os
import subprocess
import sys

from PIL import Image

# art filename token -> the 4 screen slots it fills
ARTS = [
    ('u5849273736',  'ss00', 'ss01', 'ss02', 'ss03'),
    ('shiro420',     'ss04', 'ss05', 'ss06', 'ss07'),
    ('cyberspace669', 'ss08', 'ss09', 'ss10', 'ss11'),
    ('u4375246714',  'ss12', 'ss13', 'ss14', 'ss15'),
    ('mr.relative',  'ss16', 'ss17', 'ss18', 'ss19'),
]

# (zoom, vertical anchor) per variant of an art. zoom 1.0 = full frame.
VARIANTS = [(1.0, 0.5), (1.45, 0.15), (1.45, 0.85), (1.9, 0.5)]

MARGIN = 50


def find_art(token):
    matches = [p for p in glob.glob('*_*_*') if token.lower() in os.path.basename(p).lower() and p.lower().endswith('.png')]
    if not matches:
        raise SystemExit(f'artwork for {token!r} not found (expected a *_*_*.png in the workspace root)')
    if len(matches) > 1:
        raise SystemExit(f'ambiguous art for {token!r}: {matches}')
    return matches[0]


def crop_variant(im, zoom, anchor_y):
    w, h = im.size
    cw, ch = int(w / zoom), int(h / zoom)
    x0 = (w - cw) // 2
    y0 = int((h - ch) * anchor_y)
    y0 = max(0, min(h - ch, y0))
    box = (x0, y0, x0 + cw, y0 + ch)
    return im.crop(box)


def main():
    os.makedirs('screens/ss', exist_ok=True)
    for token, *slots in ARTS:
        art = find_art(token)
        base = Image.open(art).convert('RGB')
        for (zoom, anchor), slot in zip(VARIANTS, slots):
            v = crop_variant(base, zoom, anchor)
            tmp = f'/tmp/ss-{slot}.png'
            v.save(tmp)
            out = f'screens/ss/{slot}.png'
            r = subprocess.run([sys.executable, 'tools/make_splash.py', '--logo',
                                tmp, '--out', out, '--margin', str(MARGIN)],
                               capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(f'{slot}: make_splash failed: {r.stderr or r.stdout}')
            print(f'{slot} <- {os.path.basename(art)[:22]} zoom {zoom:g} y {anchor:g} | {r.stdout.strip()}')
    print('20 screensaver screens rebuilt in screens/ss/ (600x800, 8-bit greyscale)')


if __name__ == '__main__':
    main()