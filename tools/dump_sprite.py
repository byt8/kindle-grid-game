import re
import sys

h = open('src/sprites.h').read()
ids = re.findall(r'^    "([a-z_]+/[a-z_]+)",$', h, re.M)
blobs = re.findall(r'^    \{((?:0x[0-9a-f]{2},){799}0x[0-9a-f]{2})\},$', h, re.M)

for name in sys.argv[1:]:
    i = ids.index(name)
    vals = [int(b, 16) for b in blobs[i].split(',')]
    px = [(vals[k >> 1] >> ((k & 1) * 4)) & 15 for k in range(1600)]
    tone = sum(p - 1 for p in px if p > 1)
    body = sum(1 for p in px if p)
    pct = 100 * tone // (8 * body) if body else 0
    print(f'{name}: {pct}% ink ({tone} tone-px of {body} opaque)')
    for y in range(40):
        row = ''
        for x in range(40):
            row += ' .:-=+*#%@'[px[y * 40 + x]]
        if row.strip('.') or y % 10 == 0:
            print(row)
    print()
