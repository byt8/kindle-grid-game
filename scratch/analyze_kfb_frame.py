#!/usr/bin/env python3
"""Inspect a KFB preview PGM: grey distribution (by rows) in the map window and
the control bar, plus an ASCII rendering of the game window."""
import sys
from collections import Counter

path = sys.argv[1]
with open(path, "rb") as f:
    assert f.readline().strip() == b"P5"
    w, h = map(int, f.readline().split())
    assert f.readline().strip() == b"255"
    data = f.read()
assert (w, h) == (600, 800), (w, h)

def rows(y0, y1):
    return [data[y * w:(y + 1) * w] for y in range(y0, y1)]

print(f"{path}: {w}x{h}")

# game window = bottom 408 px (rows 392..799). map = top 29 cell rows (392..740),
# message lines = 740..800.
gw = b"".join(rows(392, 800))
c = Counter(gw)
print("game-window grey histogram (top 12):")
for val, n in c.most_common(12):
    print(f"  byte {val:3d}  {n:7d}  {100.0*n/len(gw):5.2f}%")

bar = b"".join(rows(0, 392))
bc = Counter(bar)
print("control-bar grey histogram (top 8):")
for val, n in bc.most_common(8):
    print(f"  byte {val:3d}  {n:7d}  {100.0*n/len(bar):5.2f}%")

# ASCII art of the game window at cell resolution, quantised to 4 grey bands.
ramp = {255: ' ', 238: '.', 221: ':', 204: 'o', 187: '#', 170: '#',
        153: '#', 136: '#', 119: '#', 102: '#', 85: '@', 68: '@',
        51: '@', 34: '@', 17: '@', 0: '@'}
def hatch(byte):
    if byte == 255: return ' '      # paper
    if byte >= 221: return '.'      # lightest ink
    if byte >= 170: return ':'      # light mid
    if byte >= 102: return '#'      # mid
    return '@'                      # dark ink
print("\nASCII game window (map rows 392..740, 12px per cell row, 1px line):")
for y in range(392, 740, 12):
    line = ""
    for x in range(0, 600):
        line += hatch(data[y * w + x])
    print(line)
print("\nASCII message lines (740..800):")
for y in range(740, 800, 4):
    line = ""
    for x in range(0, 600):
        line += hatch(data[y * w + x])
    print(line)