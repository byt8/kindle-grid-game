#!/usr/bin/env python3
"""Probe: on a chess PGM dump, compare the mean level of ground pixels just
outside a piece's silhouette with the mean level of the piece's own ink, on a
light square and on a dark square."""
import re
import sys

T = 52
BX, BY, SQ = 28, 64, 68

pgm = sys.argv[1]
d = open(pgm, "rb").read()
k = d.index(b"\n255\n") + 5
W = int(d[:k].split()[1])
px = d[k:]

raw = open("src/chess_pieces.h").read()
blobs = re.findall(r"\{((?:0x[0-9a-f]{2},?){676})\}", raw)
D = [[int(x, 16) for x in b.strip(",").split(",")] for b in blobs]


def at(pid, x, y):
    n = y * T + x
    return (D[pid][n >> 1] >> ((n % 2) * 4)) % 16


def probe(label, fi, ri, pid):
    x0 = BX + fi * SQ + 8
    y0 = BY + (7 - ri) * SQ + 8
    ground, ink = [], []
    for dy in range(T):
        for dx in range(T):
            g = px[(y0 + dy) * W + x0 + dx]
            v = at(pid, dx, dy)
            (ink if v > 1 else ground).append(g)
    mg = sum(ground) // len(ground)
    mi = sum(ink) // max(1, len(ink))
    print("%-24s ground=%3d ink=%3d