import re
import sys

h = open('src/sprites.h').read()
ids = re.findall(r'^    "([a-z_]+/[a-z_]+)",$', h, re.M)
blobs = re.findall(r'^    \{((?:0x[0-9a-f]{2},){399}0x[0-9a-f]{2})\},$', h, re.M)

for name in sys.argv[1:]:
    i = ids.index(name)
    vals = [int(b, 16) for b in blobs[i].split(',')]
    px = [(vals[k >> 2] >> ((k & 3) * 2)) & 3 for k in range(1600)]
    ink = sum(1 for p in px if p == 2)
    print(f'{name}: ink {ink}/1600')
    for y in range(40):
        row = ''
        for x in range(40):
            v = px[y * 40 + x]
            row += '#' if v == 2 else ('.' if v == 0 else '+')
        if row.strip('.') or y % 10 == 0:
            print(row)
    print()
