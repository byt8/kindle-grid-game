#!/usr/bin/env python3
"""Tone histogram of each baked chess sprite."""
import re
from collections import Counter

NAMES = "WPAWN WROOK WKNIGHT WBISHOP WQUEEN WKING BPAWN BROOK BKNIGHT BBISHOP BQUEEN BKING".split()
T = 52
blobs = re.findall(r"\{((?:0x[0-9a-f]{2},?){676})\}", open("src/chess_pieces.h").read())
data = [[int(x, 16) for x in b.strip(",").split(",")] for b in blobs]


def at(i, x, y):
    n = y * T + x
    return (data[i][n >> 1] >> ((n % 2) * 4)) % 16


for i, name in enumerate(NAMES):
