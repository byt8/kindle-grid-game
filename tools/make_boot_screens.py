#!/usr/bin/env python3
"""Rebuild the five Kindle full-screen pictures (600x800 greyscale) in screens/.

The panel is 8-bit greyscale only (colour type 0, no alpha). The AI art is
dense ink (77-96% black pixels), which would come out as a near-black panel and
swallow the code-drawn progress bar / labels, so each piece is inverted to a
white panel with dark etched lines, then passed through the proven
`make_splash` fit (crop -> fit into 600x800 with a margin -> box-average).

Source art -> screen -> intended device file(s):
  fouloud...   -> screens/boot.png             -> /usr/share/blanket/bg_xsmall_defaultbg.png
                                                  + /opt/amazon/low_level_screens/600x800/splash.png
  danirolli... -> screens/restart-low.png      -> /opt/amazon/low_level_screens/600x800/reboot.png
  langdd...    -> screens/restart-shutdown.png -> /usr/share/blanket/shutdown/bg_xsmall_reboot.png
  bilbilsu...  -> screens/repair-update.png    -> /opt/amazon/low_level_screens/600x800/repair.png
                                                  (+ update_*.png when one screen is wanted for all)
  melira...    -> screens/misc.png             -> critbatt / shipping_mode / cust_service /
                                                  langpicker / usb / ota leftovers

Every install is a copy over the read-only root (mntroot rw ... mntroot ro) as
documented in the README; this script only builds the pictures.

Usage:
  python3 tools/make_boot_screens.py            # rebuild all five screens/
"""
import glob
import os
import subprocess
import sys

from PIL import Image, ImageOps

# art filename token (lowercase) -> screen name
MAPPING = [
    ('fouloud', 'boot'),
    ('danirolli', 'restart-low'),
    ('langdd', 'restart-shutdown'),
    ('bilbilsu', 'repair-update'),
    ('melira', 'misc'),
]

MARGIN = 50


def find_art(token):
    matches = [p for p in glob.glob('*_*_*') if token in os.path.basename(p).lower() and p.lower().endswith('.png')]
    if not matches:
        raise SystemExit(f'artwork for {token!r} not found (expected a *_*_*.png in the workspace root)')
    if len(matches) > 1:
        raise SystemExit(f'ambiguous art for {token!r}: {matches}')
    return matches[0]


def main():
    os.makedirs('screens', exist_ok=True)
    for token, name in MAPPING:
        art = find_art(token)
        inverted = f'/tmp/inv-{name}.png'
        im = Image.open(art).convert('RGB')
        im = ImageOps.invert(im)   # white canvas, dark etching
        im.save(inverted)
        out = f'screens/{name}.png'
        r = subprocess.run([sys.executable, 'tools/make_splash.py', '--logo',
                            inverted, '--out', out, '--margin', str(MARGIN)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f'{name}: make_splash failed: {r.stderr or r.stdout}')
        print(f'{name:18s} <- {os.path.basename(art):22s} | {r.stdout.strip()}')
    print('five screens rebuilt in screens/ (600x800, 8-bit greyscale, colour type 0)')


if __name__ == '__main__':
    main()
